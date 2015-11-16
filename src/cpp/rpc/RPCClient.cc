#include "RPCClient.h"
#include <errno.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <zlib.h>
#include <stdarg.h>
#ifndef ANDROID
  #include <uuid/uuid.h>
#else
  #define IOV_MAX (8)
#endif
#include <sys/time.h>
#include "RPCLogging.h"

BEGIN_MOCA_RPC_NAMESPACE

int32_t RPCClientImpl::emptyData_ = 0;

int32_t
RPCClientImpl::processErrorHook(int32_t status) const
{
  if (status == RPC_DISCONNECTED) {
    close();
    fireDisconnectedEvent();
  } else {
    fireErrorEvent(status, NULL);
  }
  return status;
}

int32_t RPCClientImpl::convertError(const char *func, const char *file, uint32_t line) const
{
  int32_t st;
  int32_t code = errno;
  switch (code) {
    case EACCES:
      st = RPC_NO_ACCESS;
      break;
    case EAFNOSUPPORT:
    case EPROTOTYPE:
      st = RPC_NOT_SUPPORTED;
      break;
    case EMFILE:
    case ENFILE:
      st = RPC_TOO_MANY_OPEN_FILE;
      break;
    case ENOBUFS:
      st = RPC_INSUFFICIENT_RESOURCE;
      break;
    case ENOMEM:
      st = RPC_OUT_OF_MEMORY;
      break;
    case EFAULT:
    case EINVAL:
    case EBADF:
      st = RPC_INVALID_ARGUMENT;
      break;
    case ENETDOWN:
    case ENETUNREACH:
    case ECONNRESET:
    case EPIPE:
    case EDESTADDRREQ:
    case ENOTCONN:
      st = RPC_DISCONNECTED;
      break;
    case EWOULDBLOCK:
      return RPC_WOULDBLOCK;
    case ETIMEDOUT:
      st = RPC_TIMEOUT;
      break;
    default:
      st = RPC_INTERNAL_ERROR;
      break;
  }

  char buffer[128];
  RPC_LOG_ERROR("System error from [%s|%s:%s] . %d:%s", func, file, line, code, strerror_r(errno, buffer, sizeof(buffer)) == 0 ? buffer : "UNKNOWN ERROR");
  return processErrorHook(st);
}

int32_t
RPCClientImpl::doWriteFully(int fd, iovec *iov, size_t *iovsize) const
{
  ssize_t wrote = 0;
  size_t left = *iovsize;
  int32_t st = RPC_OK;
  while (left > 0) {
    wrote = writev(fd, iov, left);
    if (wrote == -1) {
      if (errno == EINTR) {
        continue;
      } else {
        st = convertError(__FUNCTION__, __FILE__, __LINE__);
        break;
      }
    } else if (wrote > 0) {
      RPC_LOG_DEBUG("Wrote %zd bytes data to server", wrote);
      size_t iovcount = left;
      for (size_t idx = 0; idx < iovcount; ++idx) {
        wrote -= iov[idx].iov_len;
        if (wrote >= 0) {
          left--;
          if (wrote == 0) {
            break;
          }
        } else if (wrote < 0) {
          iov[idx].iov_base = unsafeGet<char>(iov[idx].iov_base, iov[idx].iov_len + wrote);
          iov[idx].iov_len = -wrote;
          break;
        }
      }
      if (left != iovcount) {
        iov += iovcount - left;
      }
    } else {
      break;
    }
  }

  *iovsize = *iovsize - left;
  return st;
}

int32_t
RPCClientImpl::writeFully(int fd, iovec *iov, size_t *iovsize) const
{
  size_t offset = 0;
  int32_t st = RPC_OK;
  size_t left = *iovsize;
  while (st == RPC_OK && left > 0) {
    size_t size = left;
    if (size > IOV_MAX) {
      size = IOV_MAX;
    }
    st = doWriteFully(fd, iov + offset, &size);
    offset += size;
    left -= size;
  }
  *iovsize = *iovsize - left;
  return st;
}

int32_t
RPCClientImpl::serialize(const StringLite &data, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  size_t required = sizeof(int32_t) + data.size() + 1;
  if (required > MAX_INT32) {
    return processErrorHook(RPC_INVALID_ARGUMENT);
  }
  ChainedBuffer *buffer = ChainedBuffer::checkBuffer(head, current, nextBuffer, numBuffers, required);
  *totalSize += required;
  safeWrite<>(static_cast<int32_t>(data.size()), buffer->allocate<int32_t>());
  memcpy(buffer->allocate(data.size()), data.str(), data.size());
  safeWrite<>(static_cast<uint8_t>(0), buffer->allocate<uint8_t>());
  return RPC_OK;
}

int32_t
RPCClientImpl::serialize(int64_t id, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  ChainedBuffer *buffer = ChainedBuffer::checkBuffer(head, current, nextBuffer, numBuffers, sizeof(id));
  safeWrite<>(id, buffer->allocate<int64_t>());
  *totalSize += sizeof(id);
  return RPC_OK;
}

int32_t
RPCClientImpl::serialize(const KeyValuePairs<StringLite, StringLite> &pairs, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  int32_t code;
  for (size_t idx = 0, size = pairs.size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = pairs.get(idx);
    if (MOCA_RPC_FAILED(code = serialize(pair->key, head, current, nextBuffer, totalSize, numBuffers))) {
     RPC_LOG_ERROR("Could not serialize key. %d", code);
      return code;
    }
    if (MOCA_RPC_FAILED(code = serialize(pair->value, head, current, nextBuffer, totalSize, numBuffers))) {
     RPC_LOG_ERROR("Could not serialize value. %d", code);
      return code;
    }
  }

  return RPC_OK;
}

int32_t
RPCClientImpl::deserialize(bool swap, void *buffer, size_t *available, StringLite *data)
{
  if (*available < 4) {
    return processErrorHook(RPC_CORRUPTED_DATA);
  }
  int32_t size = *unsafeGet<int32_t>(buffer, 0);
  if (swap) {
    size = INT32_SWAP(size);
  }
  if (size <= 0) {
    return processErrorHook(RPC_CORRUPTED_DATA);
  }
  if (*available < 4 + static_cast<uint32_t>(size)) {
    return processErrorHook(RPC_CORRUPTED_DATA);
  }
  data->assign(unsafeGet<char>(buffer, sizeof(int32_t)), size);
  *available -= 5 + size;
  return RPC_OK;
}

int32_t
RPCClientImpl::deserialize(bool swap, void *buffer, size_t size, KeyValuePairs<StringLite, StringLite> *pairs)
{
  int32_t code = RPC_OK;
  StringLite key;
  StringLite value;
  size_t available = size;
  while (available > 0) {
    if (MOCA_RPC_FAILED(code = deserialize(swap, unsafeGet<void>(buffer, size - available), &available, &key))) {
     RPC_LOG_ERROR("Could not deserialize key. %d", code);
      break;
    }
    if (MOCA_RPC_FAILED(code = deserialize(swap, unsafeGet<void>(buffer, size - available), &available, &value))) {
     RPC_LOG_ERROR("Could not deserialize value. %d", code);
      break;
    }
    pairs->append(key, value);
  }
  return code;
}

RPCClientImpl::RPCClientImpl(RPCLogger logger, LogLevel level, RPCOpaqueData loggerUserData)
  : wrapper_(NULL), fd_(-1), refcount_(1), timeout_(INT64_MAX),
  keepalive_(INT64_MAX), running_(false), state_(STATE_NEGOTIATION_MAGIC), currentId_(0), writeBuffer_(NULL),
  readBuffer_(NULL), nextBuffer_(&readBuffer_), readAvailable_(0), readBufferOffset_(0), pendingBuffer_(NULL),
  currentPendingBuffer_(NULL), nextPendingBuffer_(&pendingBuffer_), numPendingBuffers_(0), littleEndian_(0),
  version_(0), flags_(NEGOTIATION_FLAG_ACCEPT_ZLIB << 8), peerIdSize_(0), requestId_(0), requestFlags_(0),
  requestType_(0), requestHeaderSize_(0), requestPayloadSize_(0), dispatchedPayloadSize_(0),
  logger_(logger), level_(level), loggerUserData_(loggerUserData)
{
  if (logger_ == NULL) {
    logger_ = MocaRPCSimpleLogger;
    level_ = defaultRPCLoggerLogLevel;
    loggerUserData_ = &defaultRPCLoggerSink;
  }
  pipe_[0] = -1;
  pipe_[1] = -1;
  clearDispatcherThreadId();
}

RPCClientImpl::~RPCClientImpl()
{
  close();
  ChainedBuffer::destroy(&pendingBuffer_);
  ChainedBuffer::destroy(&readBuffer_);
  pthread_mutex_destroy(&mutex_);
  pthread_mutex_destroy(&pendingMutex_);
  pthread_mutex_destroy(&stateMutex_);
}

ChainedBuffer *
RPCClientImpl::checkAndGetBuffer()
{
  if (writeBuffer_->capacity - writeBuffer_->size == 0) {
    writeBuffer_ = ChainedBuffer::create(4096, &nextBuffer_);
  }
  return writeBuffer_;
}

int32_t
RPCClientImpl::doFireEvent(int32_t eventType, RPCOpaqueData eventData) const
{
  lock();

  for (int32_t idx = 0, size = listeners_.size(); idx < size; ++idx) {
    const EventListenerHandle *handle = listeners_.get(idx);
    if (handle->listener && (handle->mask & eventType)) {
      handle->listener(wrapper_, eventType, eventData, handle->userData);
    }
  }

  unlock();
  return RPC_OK;
}

void
RPCClientImpl::dispatchHint(PacketEventData *data)
{
  return;
}

int32_t
RPCClientImpl::firePacketEvent()
{
  PacketEventData data = { requestId_, requestCode_, requestPayloadSize_, &requestHeaders_ };
  int32_t eventType;
  switch (requestType_) {
    case PACKET_TYPE_REQUEST:
      eventType = EVENT_TYPE_REQUEST;
      break;
    case PACKET_TYPE_RESPONSE:
      eventType = EVENT_TYPE_RESPONSE;
      break;
    case PACKET_TYPE_HINT:
      dispatchHint(&data);
    default:
      return RPC_OK;
  }
  return doFireEvent(eventType, &data);
}

int32_t
RPCClientImpl::firePayloadEvent(size_t size, const char *payload, bool commit)
{
  PayloadEventData data = { requestId_, static_cast<int32_t>(size), 0, commit, payload};
  return doFireEvent(EVENT_TYPE_PAYLOAD, &data);
}

int32_t
RPCClientImpl::fireErrorEvent(int32_t status, const char *message) const
{
  if (message == NULL) {
    message = errorString(status);
  }
  ErrorEventData data = { status, message };
  return doFireEvent(EVENT_TYPE_ERROR, &data);
}

void
RPCClientImpl::checkAndClose(int32_t *fd)
{
  if (*fd > 0) {
    ::close(*fd);
    *fd = -1;
  }
}

void
RPCClientImpl::close() const
{
  checkAndClose(&fd_);
  checkAndClose(pipe_ + 0);
  checkAndClose(pipe_ + 1);
}

int32_t
RPCClientImpl::doSendPacketDispatcherThread(int64_t id, int32_t code, int32_t type,
    const KeyValuePairs<StringLite, StringLite> *header, const void *payload, size_t payloadSize) const
{
  int32_t st = RPC_OK;
  addRef();
  lockPending();
  ChainedBuffer::checkBuffer(&pendingBuffer_, &currentPendingBuffer_, &nextPendingBuffer_, &numPendingBuffers_, sizeof(Header));
  Header *hdr = currentPendingBuffer_->allocate<Header>();
  hdr->id = id;
  hdr->code = code;
  hdr->flags = type & 0xFF;
  hdr->payloadSize = payloadSize;
  hdr->headerSize = 0;
  if (header) {
    MOCA_RPC_DO_GOTO(st, serialize(*header, &pendingBuffer_, &currentPendingBuffer_, &nextPendingBuffer_, &hdr->headerSize, &numPendingBuffers_), cleanupExit)
  }
  if (payloadSize > 0 && payload) {
    ChainedBuffer::checkBuffer(&pendingBuffer_, &currentPendingBuffer_, &nextPendingBuffer_, &numPendingBuffers_, payloadSize);
    memcpy(currentPendingBuffer_->allocate(payloadSize), payload, payloadSize);
  }

cleanupExit:
  unlockPending();
  this->release();
  return st;
}

int32_t
RPCClientImpl::doSendPacketOtherThread(int64_t id, int32_t code, int32_t type,
    const KeyValuePairs<StringLite, StringLite> *header, const void *payload, size_t payloadSize) const
{
  iovec *iov = NULL;
  size_t iovsize = 0;
  ChainedBuffer *head = NULL;
  ChainedBuffer *buffer = NULL;
  ChainedBuffer **nextBuffer = &head;
  int32_t numBuffers = 0;
  ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers, MOCA_RPC_ALIGN(payloadSize + sizeof(Header), 4096));

  int32_t st;
  bool release = false;
  size_t total;;
  Header *hdr = buffer->allocate<Header>();
  hdr->id = id;
  hdr->code = code;
  hdr->flags = type & 0xFF;
  hdr->payloadSize = payloadSize;
  hdr->headerSize = 0;

  if (header) {
    MOCA_RPC_DO_GOTO(st, serialize(*header, &head, &buffer, &nextBuffer, &hdr->headerSize, &numBuffers), cleanupExit)
  }
  iov = static_cast<iovec *>(malloc(sizeof(iovec) * (numBuffers + 1)));

  buffer = head;
  while (buffer) {
    iov[iovsize].iov_base = buffer->buffer;
    iov[iovsize].iov_len = buffer->size;
    ++iovsize;
    buffer = buffer->next;
  }
  if (payloadSize > 0 && payload) {
    iov[iovsize].iov_base = const_cast<void *>(payload);
    iov[iovsize].iov_len = payloadSize;
    ++iovsize;
  }

  addRef();
  release = true;
  total = iovsize;
  if (MOCA_RPC_FAILED(st = writeFully(fd_, iov, &iovsize))) {
    goto cleanupExit;
  }

  if (total != iovsize) {
    transferToPending(iov + iovsize, total - iovsize);
    notifyDispatcherThread();
  }

cleanupExit:
  ChainedBuffer::destroy(&head);

  if (release) {
    this->release();
  }
  return st;
}

void
RPCClientImpl::transferToPending(const iovec *iov, size_t iovsize) const
{
  lockPending();
  for (size_t idx = 0; idx < iovsize; ++idx) {
    ChainedBuffer::checkBuffer(&pendingBuffer_, &currentPendingBuffer_, &nextPendingBuffer_, &numPendingBuffers_, iov->iov_len);
    memcpy(currentPendingBuffer_->allocate(iov[idx].iov_len), iov[idx].iov_base, iov[idx].iov_len);
  }
  unlockPending();
}

int32_t
RPCClientImpl::initBuffer()
{
  writeBuffer_ = ChainedBuffer::create(4000, &nextBuffer_);
  return RPC_OK;
}

int32_t
RPCClientImpl::initLocalId()
{
#ifndef ANDROID
  uuid_t uuid;
  uuid_generate(uuid);
  char uuidStr[64];
  uuid_unparse(uuid, uuidStr);
  localId_.assign(uuidStr);
#else
  char buffer[32];
  for (int idx = 0; idx < 8; ++idx) {
    snprintf(buffer, sizeof(buffer), "%X", rand());
    localId_.append(buffer);
  }
#endif
  return RPC_OK;
}

int32_t
RPCClientImpl::initMultiThread()
{
  int32_t st = RPC_OK;
  pthread_mutexattr_t mutexattr;
  if (pthread_mutexattr_init(&mutexattr)) {
    st = convertError(__FUNCTION__, __FILE__, __LINE__);
    goto cleanupExit;
  }
  if (pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE)) {
    st = convertError(__FUNCTION__, __FILE__, __LINE__);
    goto cleanupExit;
  }
  if (pthread_mutex_init(&mutex_, &mutexattr)) {
    st = convertError(__FUNCTION__, __FILE__, __LINE__);
  }
  if (pthread_mutex_init(&pendingMutex_, NULL)) {
    st = convertError(__FUNCTION__, __FILE__, __LINE__);
  }
  if (pthread_mutex_init(&stateMutex_, NULL)) {
    st = convertError(__FUNCTION__, __FILE__, __LINE__);
  }

cleanupExit:
  pthread_mutexattr_destroy(&mutexattr);
  return st;
}

int32_t
RPCClientImpl::connect(const char *address)
{
  StringLite addressCopy(address);
  const char *ptr = addressCopy.str();
 RPC_LOG_DEBUG("Connecting to %s", address);
  while (*ptr && *ptr != ':') {
    ++ptr;
  }
  if (*ptr == '\0') {
   RPC_LOG_ERROR("Invalid server address %s", address);
    return processErrorHook(RPC_INVALID_ARGUMENT);
  }
  const char *host = addressCopy.str();
  const char *port = ptr;
  *const_cast<char *>(port) = '\0';
  ++port;
  addrinfo *result = NULL, *tmp = NULL;
  addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int32_t st = getaddrinfo(host, port, &hints, &tmp);
  fd_ = -1;
  if (st) {
   RPC_LOG_ERROR("Could not get address info for %s", address);
    return processErrorHook(RPC_INVALID_ARGUMENT);
  }
  struct timeval realTimeout = {static_cast<time_t>(timeout_ / 1000000), static_cast<suseconds_t>(timeout_ % 1000000)};
  const int32_t enabled = 1;
  time_t deadline = getDeadline(timeout_);
  st = RPC_OK;
  for (result = tmp; result; result = result->ai_next) {
    if (isTimeout(deadline)) {
      st = RPC_TIMEOUT;
     RPC_LOG_ERROR("Connecting to %s timeout", address);
      goto cleanupExit;
    }
    fd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd_ < 0) {
     RPC_LOG_ERROR("Could not create socket");
      continue;
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &realTimeout, sizeof(realTimeout)) ||
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &realTimeout, sizeof(realTimeout)) ||
        setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) ||
#ifdef SO_NOSIGPIPE
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) ||
#endif
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled))) {
     RPC_LOG_ERROR("Could not set socket options. %d", errno);
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if (::connect(fd_, result->ai_addr, result->ai_addrlen) < 0) {
     RPC_LOG_ERROR("Could not connect to remote server. %d", errno);
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if (fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK) == -1) {
     RPC_LOG_ERROR("Could not set socket to nonblocking mode. %d", errno);
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    st = RPC_OK;
    break;
  }

  if (fd_ != -1) {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, 0);

    fdset_[1].fd = fd_;
    fdset_[1].events = POLLHUP | POLLIN;
   RPC_LOG_INFO("Channel to %s is connected", address);
    fireConnectedEvent();
    st = sendNegotiation();
   RPC_LOG_INFO("Sent negotiation to %s", address);
  }

cleanupExit:
  if (tmp) {
    freeaddrinfo(tmp);
  }
  if (fd_ < 0) {
    processErrorHook(st = RPC_CAN_NOT_CONNECT);
  }
  return st;
}

template<typename T>
void
RPCClientImpl::readPrimitiveSink(void *data, size_t size, void *userData)
{
  PrimitiveSinkArgument<T> *argument = static_cast<PrimitiveSinkArgument<T> *>(userData);
  if (size == sizeof(T)) {
    *argument->result = safeRead<T>(data);
  } else {
    memcpy(unsafeGet<char *>(argument->result, argument->offset), data, size);
    argument->offset += size;
  }
}

int32_t
RPCClientImpl::doRead(size_t size, ReadDataSink sink, void *sinkUserData, int32_t nextState)
{
  if (readAvailable_ < size) {
   RPC_LOG_TRACE("There are %zd bytes data available when %zd is expected.", readAvailable_, size);
    return RPC_WOULDBLOCK;
  }
  readAvailable_ -= size;
  size_t offset = 0;
  while (size > 0) {
    size_t available = readBuffer_->size - readBufferOffset_;
    if (available > size) {
      available = size;
    }
    sink(readBuffer_->buffer + readBufferOffset_, available, sinkUserData);
    readBufferOffset_ += available;
    size -= available;
    offset += available;
    if (readBufferOffset_ == static_cast<size_t>(readBuffer_->size) && readBuffer_ != writeBuffer_) {
      ChainedBuffer *next = readBuffer_->next;
      free(readBuffer_);
      readBuffer_ = next;
      readBufferOffset_ = 0;
    }
  }
  if (state_ != nextState) {
    setState(nextState);
  }
  return RPC_OK;
}

void
RPCClientImpl::readToBufferSink(void *data, size_t size, void *userData)
{
  BufferSinkArgument *argument = static_cast<BufferSinkArgument *>(userData);
  memcpy(unsafeGet<char *>(argument->buffer, argument->offset), data, size);
  argument->offset += size;
}

void
RPCClientImpl::readToStringSink(void *data, size_t size, void *userData)
{
  StringLite *string = static_cast<StringLite *>(userData);
  string->append(static_cast<char *>(data), size);
}

int32_t
RPCClientImpl::doReadNegotiationMagic()
{
  int32_t magic;
  int32_t st;
  if (MOCA_RPC_FAILED(st = doRead(&magic, STATE_NEGOTIATION_FLAGS))) {
    return st;
  }
  switch (magic) {
    case 0x4D4F4341:
      littleEndian_ = false;
      break;
    case 0x41434F4D:
      littleEndian_ = true;
      break;
    default:
      return processErrorHook(RPC_INCOMPATIBLE_PROTOCOL);
  }
  return RPC_OK;
}

int32_t
RPCClientImpl::doReadNegotiationFlags()
{
  int32_t st;
  if (MOCA_RPC_FAILED(st = doRead(&flags_, STATE_NEGOTIATION_PEER_ID_SIZE))) {
    return st;
  }
  version_ = flags_ & 0xFF;
  flags_ >>= 8;
  return RPC_OK;
}

int32_t
RPCClientImpl::doReadNegotiationPeerId()
{
  int32_t st = doRead(peerIdSize_, &peerId_, STATE_PACKET_ID);
  if (state_ == STATE_PACKET_ID) {
    st = fireEstablishedEvent();
  }
  return st;
}


int32_t
RPCClientImpl::doReadPacketPayloadSize()
{
  int32_t st = doRead(&requestPayloadSize_, STATE_PACKET_HEADER);
  if (state_ == STATE_PACKET_HEADER) {
    if (requestHeaderSize_ < 0 || requestPayloadSize_ < 0) {
      return processErrorHook(RPC_INCOMPATIBLE_PROTOCOL);
    }
    if (requestHeaderSize_ == 0) {
      st = firePacketEvent();
      if (requestPayloadSize_ == 0) {
        setState(STATE_START_OVER);
      } else {
        setState(STATE_PACKET_PAYLOAD);
      }
    }
  }
  return st;
}

int32_t
RPCClientImpl::doReadCompressedPacketHeader()
{
  int32_t st;
  int32_t state = state_;
  int32_t originalSize;
  Bytef *readBuffer = static_cast<Bytef *>(malloc(requestHeaderSize_));
  Bytef *decompressedBuffer = NULL;
  char *ptr;
  int32_t size;
  uLongf tmp;
  StringLite key;
  StringLite value;
  MOCA_RPC_DO_GOTO(st, doRead(requestHeaderSize_, readBuffer, state), cleanupExit);

  if (requestPayloadSize_ == 0) {
    setState(STATE_START_OVER);
  } else {
    setState(STATE_PACKET_PAYLOAD);
  }

  if (requestHeaderSize_ < 4 + 1) {
    st = RPC_CORRUPTED_DATA;
    goto cleanupExit;
  }


  originalSize = safeRead<int32_t>(readBuffer);
  /* TODO make this configurable */
  if (originalSize > 67108864) {
    st = RPC_CORRUPTED_DATA;
    goto cleanupExit;
  }
  ptr = reinterpret_cast<char *>(decompressedBuffer = static_cast<Bytef *>(malloc(originalSize)));
  tmp = originalSize;
  if (uncompress(decompressedBuffer, &tmp, readBuffer + sizeof(int32_t), requestHeaderSize_ - sizeof(int32_t)) != Z_OK) {
   RPC_LOG_ERROR("Corrupted data from server");
    st = RPC_CORRUPTED_DATA;
    goto cleanupExit;
  }

  while (originalSize > 0) {
    size = safeRead<int32_t>(ptr);
    ptr += sizeof(int32_t);
    key.append(ptr, size);
    ptr += size + 1;
    originalSize -= (size + 1 + sizeof(int32_t));
    size = safeRead<int32_t>(ptr);
    ptr += sizeof(int32_t);
    value.append(ptr, size);
    ptr += size + 1;
    originalSize -= (size + 1 + sizeof(int32_t));
    requestHeaders_.append(key, value);
  }

  st = firePacketEvent();
  requestHeaders_.clear();

cleanupExit:
  if (decompressedBuffer) {
    free(decompressedBuffer);
  }
  if (readBuffer) {
    free(readBuffer);
  }

  return st;
}

int32_t
RPCClientImpl::doReadPlainPacketHeader()
{
  size_t endReadAvailable = readAvailable_ - requestHeaderSize_;

  int32_t st;
  int32_t size;
  StringLite key;
  StringLite value;
  int32_t state = state_;
  while (readAvailable_ > endReadAvailable) {
    if (MOCA_RPC_FAILED(st = doRead(&size, state))) {
      return st;
    }
    if (MOCA_RPC_FAILED(st = doRead(size, &key, state))) {
      return st;
    }
    if (MOCA_RPC_FAILED(st = doRead(&size, state))) {
      return st;
    }
    if (MOCA_RPC_FAILED(st = doRead(size, &value, state))) {
      return st;
    }
    requestHeaders_.append(key, value);
  }

  st = firePacketEvent();
  requestHeaders_.clear();
  if (requestPayloadSize_ == 0) {
    setState(STATE_START_OVER);
  } else {
    setState(STATE_PACKET_PAYLOAD);
  }
  return st;
}

int32_t
RPCClientImpl::doReadPacketHeader()
{
  if (readAvailable_ < static_cast<size_t>(requestHeaderSize_)) {
    return RPC_WOULDBLOCK;
  }

  if (requestFlags_ & PACKET_FLAG_HEADER_ZLIB_ENCODED) {
    return doReadCompressedPacketHeader();
  } else {
    return doReadPlainPacketHeader();
  }
}

void
RPCClientImpl::firePayloadEventSink(void *data, size_t size, void *userData)
{
  FirePayloadEventSinkArgument *argument = static_cast<FirePayloadEventSinkArgument *>(userData);
  bool commit = argument->commit && (static_cast<size_t>(argument->pending) == argument->dispatched + size);

  argument->status = argument->client->firePayloadEvent(size, static_cast<char *>(data), commit);
  argument->dispatched += size;
}

int32_t
RPCClientImpl::doReadPacketPayload()
{
  if (readAvailable_ == 0) {
    return RPC_WOULDBLOCK;
  }

  int32_t pending = requestPayloadSize_ - dispatchedPayloadSize_;
  bool commit = static_cast<size_t>(pending) <= readAvailable_;
  if (static_cast<size_t>(pending) > readAvailable_) {
    pending = readAvailable_;
  }
  FirePayloadEventSinkArgument argument = { this, commit, dispatchedPayloadSize_, pending, RPC_OK};
  doRead(pending, firePayloadEventSink, &argument, commit ? STATE_START_OVER : state_);
  if (commit) {
    dispatchedPayloadSize_ = 0;
  } else {
    dispatchedPayloadSize_ += pending;
  }
  return argument.status;
}

int32_t
RPCClientImpl::doStartOver()
{
  requestId_ = 0;
  requestFlags_ = 0;
  requestHeaderSize_ = 0;
  dispatchedPayloadSize_ = 0;
  setState(STATE_PACKET_ID);
  return RPC_OK;
}

int32_t
RPCClientImpl::fillBuffer()
{
  int32_t st = RPC_OK;

  ChainedBuffer *buffer;
  bool done = false;

  while ((buffer = checkAndGetBuffer())) {
    int32_t bufferAvailable = buffer->capacity - buffer->size;
    int32_t rd = read(fd_, buffer->buffer + buffer->size, bufferAvailable);
    if (rd > 0) {
      if (rd < bufferAvailable) {
        /* done with whatever data in tcp buffer */
        done = true;
      }
      buffer->size += rd;
      readAvailable_ += rd;
      RPC_LOG_INFO("Read %d bytes data out of tcp buffer", rd);
      if (done) {
        break;
      }
    } else if (rd == 0) {
      return processErrorHook(RPC_DISCONNECTED);
    } else {
      if (errno == EINTR) {
        continue;
      } else {
        return convertError(__FUNCTION__, __FILE__, __LINE__);
      }
    }
  }

  return st;
}

int32_t
RPCClientImpl::doRead()
{
  int32_t st = RPC_OK;
  if (MOCA_RPC_FAILED(st = fillBuffer())) {
    return st;
  }
  while (st == RPC_OK) {
    switch (state_) {
      case STATE_NEGOTIATION_MAGIC:
        st = doReadNegotiationMagic();
        break;
      case STATE_NEGOTIATION_FLAGS:
        st = doReadNegotiationFlags();
        break;
      case STATE_NEGOTIATION_PEER_ID_SIZE:
        st = doReadNegotiationPeerIdSize();
        break;
      case STATE_NEGOTIATION_PEER_ID:
        st = doReadNegotiationPeerId();
        break;
      case STATE_PACKET_ID:
        st = doReadPacketId();
        break;
      case STATE_PACKET_CODE:
        st = doReadPacketCode();
        break;
      case STATE_PACKET_FLAGS:
        st = doReadPacketFlags();
        break;
      case STATE_PACKET_HEADER_SIZE:
        st = doReadPacketHeaderSize();
        break;
      case STATE_PACKET_PAYLOAD_SIZE:
        st = doReadPacketPayloadSize();
        break;
      case STATE_PACKET_HEADER:
        st = doReadPacketHeader();
        break;
      case STATE_PACKET_PAYLOAD:
        st = doReadPacketPayload();
        break;
      case STATE_START_OVER:
        st = doStartOver();
        break;
    }
  }
  if (st == RPC_WOULDBLOCK) {
    st = RPC_OK;
  }
  return st;
}

bool
RPCClientImpl::releaseLocked() const
{
  if (--refcount_ == 0) {
    delete this;
    return true;
  } else {
    return false;
  }
}

int32_t
RPCClientImpl::getState() const
{
  lock();
  int32_t state = state_;
  unlock();
  return state;
}

int64_t
RPCClientImpl::nextRequestId() const
{
  lock();
  int64_t id = currentId_++;
  unlock();
  return id;
}

void
RPCClientImpl::addRef() const
{
  lock();
  addRefLocked();
  unlock();
}

void
RPCClientImpl::addRefLocked() const
{
  ++refcount_;
}

bool
RPCClientImpl::release() const
{
  lock();
  bool released = releaseLocked();
  if (!released) {
    unlock();
  }
  return released;
}

bool
RPCClientImpl::isRunning() const
{
  lock();
  bool running = isRunningLocked();
  unlock();
  return running;
}

void
RPCClientImpl::setRunning(bool running)
{
  lock();
  setRunningLocked(running);
  unlock();
  return;
}

int32_t
RPCClientImpl::sendNegotiation()
{
  iovec iov[4];
  iov[0].iov_base = const_cast<uint32_t *>(&MAGIC_CODE);
  iov[0].iov_len = sizeof(MAGIC_CODE);
  iov[1].iov_base = const_cast<int32_t *>(&flags_);
  iov[1].iov_len = sizeof(flags_);
  int32_t size = localId_.size();
  iov[2].iov_base = const_cast<int32_t *>(&size);
  iov[2].iov_len = sizeof(size);
  iov[3].iov_base = const_cast<char *>(localId_.str());
  iov[3].iov_len = size + 1;

  size_t iovsize = sizeof(iov) / sizeof(iov[0]);
  /* TODO handle partial write */
  return writeFully(fd_, iov, &iovsize);
}

int32_t
RPCClientImpl::init(RPCClient *wrapper, int64_t timeout, int64_t keepalive, int32_t flags)
{
  int32_t rc;
  timeout_ = timeout < SECOND ? SECOND : timeout;
  keepalive_ = keepalive < SECOND ? SECOND : keepalive;
  if (MOCA_RPC_FAILED(rc = initBuffer())) {
    RPC_LOG_ERROR("Could not initialize buffer. %d", rc);
    return rc;
  }
  if (MOCA_RPC_FAILED(rc = initMultiThread())) {
    RPC_LOG_ERROR("Could not initialize threading support. %d", rc);
    return rc;
  }
  if (MOCA_RPC_FAILED(rc = initLocalId())) {
    RPC_LOG_ERROR("Could not initialize local channel id. %d", rc);
    return rc;
  }
  if (pipe(pipe_)) {
    return convertError(__FUNCTION__, __FILE__, __LINE__);
  }
  if (fcntl(pipe_[0], F_SETFL, fcntl(pipe_[0], F_GETFL, 0) | O_NONBLOCK) == -1) {
    return convertError(__FUNCTION__, __FILE__, __LINE__);
  }
  fdset_[0].fd = pipe_[0];
  fdset_[0].events = POLLIN;
  wrapper_ = wrapper;
  return RPC_OK;
}

void
RPCClientImpl::lock(pthread_mutex_t *mutex)
{
  pthread_mutex_lock(mutex);
}

void
RPCClientImpl::unlock(pthread_mutex_t *mutex)
{
  pthread_mutex_unlock(mutex);
}

int32_t
RPCClientImpl::addListener(EventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  RPC_LOG_TRACE("Add listener %p:%p with event mask %d", listener, userData, eventMask);
  lock();
  int32_t st = RPC_OK;
  EventListenerHandle newHandle(listener, userData, eventMask);
  for (size_t idx = 0, size = listeners_.size(); idx < size; ++idx) {
    EventListenerHandle &handle = listeners_.get(idx)->value;
    if (handle.listener == listener) {
      st = RPC_INVALID_ARGUMENT;
      goto cleanupExit;
    }
  }

  for (size_t idx = 0, size = listeners_.size(); idx < size; ++idx) {
    EventListenerHandle &handle = listeners_.get(idx)->value;
    if (handle.listener == NULL) {
      handle.listener = listener;
      handle.userData = userData;
      handle.mask = eventMask;
      goto cleanupExit;
    }
  }

  listeners_.append(listeners_.size(), newHandle);
  addRefLocked();

cleanupExit:
  unlock();

  return st;
}

int32_t
RPCClientImpl::removeListener(EventListener listener)
{
  RPC_LOG_TRACE("Remove listener %p", listener);
  lock();

  bool unlock = true;

  for (size_t idx = 0, size = listeners_.size(); idx < size; ++idx) {
    EventListenerHandle &handle = listeners_.get(idx)->value;
    if (handle.listener == listener) {
      handle.listener = NULL;
      unlock = !releaseLocked();
      goto cleanupExit;
    }
  }

cleanupExit:
  if (unlock) {
    this->unlock();
  }

  return RPC_OK;
}

int32_t
RPCClientImpl::checkAndSendPendingPackets()
{
  lockPending();
  ChainedBuffer *buffer;
  size_t iovsize = 0;
  size_t totalSize = 0;
  iovec *iov = NULL;
  int32_t st = RPC_OK;
  if (numPendingBuffers_ == 0) {
    goto cleanupExit;
  }
  buffer = pendingBuffer_;
  iov = static_cast<iovec *>(malloc(sizeof(iovec) * numPendingBuffers_));
  while (buffer) {
    iov[iovsize].iov_base = buffer->buffer;
    iov[iovsize].iov_len = buffer->size;
    totalSize += buffer->size;
    ++iovsize;
    buffer = buffer->next;
  }
  RPC_LOG_TRACE("Send %zd bytes %zd buffered pending packets", totalSize, iovsize);
  totalSize = iovsize;
  st = writeFully(fd_, iov, &iovsize);
  if (totalSize != iovsize) {
    buffer = pendingBuffer_;
    for (size_t idx = 0; idx < iovsize; ++idx) {
      buffer = buffer->next;
    }
    buffer->size = iov[iovsize].iov_len;
    memmove(buffer->buffer, iov[iovsize].iov_base, buffer->size);
    RPC_LOG_TRACE("There are %zd buffered pending packets not sent", totalSize - iovsize);
  } else {
    nextPendingBuffer_ = &pendingBuffer_;
    RPC_LOG_TRACE("All %zd buffered pending packets are sent", iovsize);
  }
  ChainedBuffer::destroy(&pendingBuffer_, iovsize);
  currentPendingBuffer_ = pendingBuffer_;
  numPendingBuffers_ = totalSize - iovsize;
cleanupExit:
  unlockPending();
  if (iov) {
    free(iov);
  }
  return st;
}

int32_t
RPCClientImpl::loop(int32_t flags)
{
  int32_t st = RPC_OK;
  lock();
  int64_t timeout = (flags & RPCClient::LOOP_FLAG_NONBLOCK) ? 0 : timeout_;
  int64_t interval = timeout < keepalive_ ? timeout_ : keepalive_;
  int32_t realTimeout = interval == INT64_MAX ? INT32_MAX : interval / 1000;
  bool hasRead = false;
  if (running_) {
    RPC_LOG_ERROR("There is already another thread running loop");
    st = RPC_ILLEGAL_STATE;
    goto cleanupExit;
  }
  updateDispatcherThreadId();
  addRefLocked();
  running_ = true;
  unlock();

  while (isRunning() && st == RPC_OK) {
    fdset_[0].revents = 0;
    fdset_[1].revents = 0;
    timeval start;
    if (realTimeout > 0) {
      gettimeofday(&start, NULL);
    }
    st = poll(fdset_, 2, realTimeout);
    if (st > 0) {
      if (fdset_[1].revents & POLLIN) {
        if (!hasRead) {
          hasRead = true;
        }
        RPC_LOG_DEBUG("Recevied data from server");
        st = doRead();
      }
      if (fdset_[0].revents & POLLIN) {
        while (read(pipe_[0], &emptyData_, sizeof(emptyData_)) > 0);
      }
      st = RPC_OK;
    } else if (st == 0) {
      if ((flags & RPCClient::LOOP_FLAG_NONBLOCK)) {
        if (!hasRead) {
          st = RPC_WOULDBLOCK;
        } else {
          break;
        }
      } else {
        timeval now;
        gettimeofday(&now, NULL);
        if ((now.tv_sec - start.tv_sec) * 1000000L + (now.tv_usec - start.tv_usec) > timeout_) {
          st = RPC_TIMEOUT;
        } else if (getState() > STATE_NEGOTIATION_PEER_ID) {
          st = keepalive();
        }
      }
    } else {
      st = convertError(__FUNCTION__, __FILE__, __LINE__);
    }
    if (st == RPC_OK) {
      st = checkAndSendPendingPackets();
    }
    if (flags & RPCClient::LOOP_FLAG_ONE_SHOT) {
      break;
    }
  }

  lock();
  clearDispatcherThreadId();
  running_ = false;

cleanupExit:
  releaseLocked();
  unlock();

  return st;
}

int32_t
RPCClientImpl::getAddress(GetSocketAddress get, StringLite *address, uint16_t *port) const
{
  sockaddr_in addr;
  socklen_t size = sizeof(addr);
  int32_t rc = get(fd_, reinterpret_cast<sockaddr *>(&addr), &size);
  if (rc != 0) {
    return convertError(__FUNCTION__, __FILE__, __LINE__);
  }

  if (address) {
    char buffer[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer));
    address->assign(buffer);
  }
  if (port) {
    *port = ntohs(addr.sin_port);
  }

  return RPC_OK;
}

int32_t
RPCClientImpl::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return getAddress(getsockname, localAddress, port);
}

int32_t
RPCClientImpl::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return getAddress(getpeername, remoteAddress, port);
}

int32_t
RPCClientImpl::localId(StringLite *localId) const
{
  *localId = localId_;
  return RPC_OK;
}

int32_t
RPCClientImpl::remoteId(StringLite *remoteId) const
{
  *remoteId = peerId_;
  return RPC_OK;
}

void
RPCClientImpl::notifyDispatcherThread() const
{
  lock();
  addRefLocked();
  unlock();

  write(pipe_[1], &emptyData_, sizeof(emptyData_));

  release();
}

int32_t
RPCClientImpl::breakLoop()
{
  lock();
  addRefLocked();
  if (running_) {
    running_ = false;
  }
  unlock();

  write(pipe_[1], &emptyData_, sizeof(emptyData_));

  release();

  return RPC_OK;
}

RPCClient::RPCClient() : impl_(NULL) { }
RPCClient::~RPCClient()
{
}

RPCClient *
RPCClient::create(int64_t timeout, int64_t keepalive, int32_t flags, RPCLogger logger, LogLevel level, RPCOpaqueData loggerUserData)
{
  RPCClientImpl* impl = new RPCClientImpl(logger, level, loggerUserData);
  RPCClient *client = new RPCClient;
  if (MOCA_RPC_FAILED(impl->init(client, timeout, keepalive, flags))) {
    delete client;
    delete impl;
    return NULL;
  }
  client->impl_ = impl;
  return client;
}

int32_t
RPCClient::addListener(EventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  return impl_->addListener(listener, userData, eventMask);
}

int32_t
RPCClient::removeListener(EventListener listener)
{
  return impl_->removeListener(listener);
}

int32_t
RPCClient::loop(int32_t flags)
{
  return impl_->loop(flags);
}

int32_t
RPCClient::breakLoop()
{
  return impl_->breakLoop();
}

int32_t
RPCClient::response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->response(id, code, headers, payload, payloadSize);
}

int32_t
RPCClient::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize);
}

int32_t
RPCClient::connect(const char *address)
{
  return impl_->connect(address);
}

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
int32_t
RPCClient::response(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(*headers, &tmpHeaders);
    return impl_->response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return impl_->response(id, code, NULL, payload, payloadSize);
  }
}

int32_t
RPCClient::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(*headers, &tmpHeaders);
    return impl_->request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return impl_->request(id, code, NULL, payload, payloadSize);
  }
}
#endif

int32_t
RPCClient::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return impl_->localAddress(localAddress, port);
}

int32_t
RPCClient::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return impl_->remoteAddress(remoteAddress, port);
}

int32_t
RPCClient::localId(StringLite *localId) const
{
  return impl_->localId(localId);
}

int32_t
RPCClient::remoteId(StringLite *remoteId) const
{
  return impl_->remoteId(remoteId);
}

void
RPCClient::addRef()
{
  impl_->addRef();
}

bool
RPCClient::release()
{
  bool released = impl_->release();
  if (released) {
    delete this;
  }
  return released;
}

int32_t
RPCClient::close()
{
  impl_->close();
  return RPC_OK;
}

int32_t
RPCClient::keepalive()
{
  return impl_->keepalive();
}

END_MOCA_RPC_NAMESPACE
