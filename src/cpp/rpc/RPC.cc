#include "RPC.h"
#include <errno.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <zlib.h>
#include <zlib.h>
#ifndef ANDROID
  #include <uuid/uuid.h>
#else
  #define IOV_MAX (8)
#endif
#include <poll.h>
#include <sys/time.h>

BEGIN_MOCA_RPC_NAMESPACE

#define INT32_SWAP(value) \
  ( \
   ((value & 0x000000ffUL) << 24) | \
   ((value & 0x0000ff00UL) << 8) | \
   ((value & 0x00ff0000UL) >> 8) | \
   ((value & 0xff000000UL) >> 24) \
  )

#define SECOND (1000000)

#define OFFSET_OF(TYPE, FIELD) (((size_t)(&((TYPE *) sizeof(TYPE))->FIELD)) - sizeof(TYPE))
#define CHAINED_BUFFER_SIZE(size) MOCA_ALIGN(size + OFFSET_OF(ChainedBuffer, buffer), 8)

static const size_t MAX_INT32 = 0x7FFFFFFF;
/* magic code is MOCA in ascii */
static const uint32_t MAGIC_CODE = 0X4D4F4341;

template<typename T>
inline T *unsafeGet(void *base, size_t offset)
{
  return reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

struct ChainedBuffer
{
  ChainedBuffer *next;
  int32_t size;
  int32_t capacity;
  uint8_t buffer[sizeof(int64_t)];

  int32_t available()
  {
    return capacity - size;
  }

  template <typename T>
  T *allocate()
  {
    return allocate<T>(sizeof(T));
  }

  template <typename T>
  T *allocate(size_t sz)
  {
    return unsafeGet<T>(allocate(sz), 0);
  }

  uint8_t *allocate(size_t sz)
  {
    uint8_t *result = buffer + size;
    size += sz;
    return result;
  }

  ChainedBuffer(int32_t c) : next(NULL), size(0), capacity(c)
  {
  }

  static void destroy(ChainedBuffer **bufferAddress, size_t limit = UINTPTR_MAX)
  {
    ChainedBuffer *next;
    ChainedBuffer *buffer = *bufferAddress;
    while (buffer && limit > 0) {
      next = buffer->next;
      free(buffer);
      buffer = next;
      --limit;
    }
    *bufferAddress = buffer;
  }

  static ChainedBuffer *create(int32_t size, ChainedBuffer ***nextAddress)
  {
    int32_t allocationSize = CHAINED_BUFFER_SIZE(size);
    int32_t capacity = allocationSize - OFFSET_OF(ChainedBuffer, buffer);
    ChainedBuffer *newBuffer = new (malloc(allocationSize)) ChainedBuffer(capacity);
    **nextAddress = newBuffer;
    *nextAddress = &newBuffer->next;
    return newBuffer;
  }

  static ChainedBuffer *checkBuffer(ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextAddress, int32_t *numBuffers, size_t required)
  {
    if (!*current || (*current)->available() < static_cast<int32_t>(required)) {
      *current = ChainedBuffer::create(required, nextAddress);
      if (!*head) {
        *head = *current;
      }
      (*numBuffers)++;
    }
    return *current;
  }

};

struct Header
{
  int64_t id;
  int32_t code;
  int32_t flags;
  int32_t headerSize;
  int32_t payloadSize;
};

void
StringLite::resize(size_t size)
{
  if (size_ < size) {
    if (capacity_ < size + 1) {
      capacity_ += MOCA_ALIGN(size + 1, 8);
      content_ = static_cast<char *>(realloc(content_, capacity_));
    }
    memset(content_ + size_, ' ', size - size_);
  }
  size_ = size;
  content_[size_] = '\0';
}

void
StringLite::append(const char *str, size_t len)
{
  if (capacity_ < size_ + len + 1) {
    capacity_ += MOCA_ALIGN(size_ + len + 1, 8);
    content_ = static_cast<char *>(realloc(content_, capacity_));
  }
  memcpy(content_ + size_, str, len);
  size_ += len;
  content_[size_] = '\0';
}

void
StringLite::assign(const char *str, size_t len)
{
  if (capacity_ < len + 1 || (content_ && capacity_ > len + 16)) {
    free(content_);
    capacity_ = MOCA_ALIGN(len + 1, 8);
    content_ = static_cast<char *>(malloc(capacity_));
  }
  memcpy(content_, str, len);
  content_[len] = '\0';
  size_ = len;
}

time_t getDeadline(int64_t timeout)
{
  time_t sec = timeout / 1000000;
  if (sec <= 0) {
    sec = 1;
  }
  return time(NULL) + sec;
}

bool isTimeout(time_t deadline)
{
  return time(NULL) >= deadline;
}

#ifdef MOCA_RPC_HAS_STL
void convert(const KeyValueMap &input, KeyValuePairs<StringLite, StringLite> *output)
{
  for (KeyValueMapConstIterator it = input.begin(), itend = input.end(); it != itend; ++it) {
    output->append(it->first, it->second);
  }
}

void convert(const KeyValuePairs<StringLite, StringLite> &input, KeyValueMap *output)
{
  for (size_t idx = 0, size = input.size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = input.get(idx);
    output->insert(make_pair<>(pair->key.str(), pair->value.str()));
  }
}
#endif

const char *errorString(int32_t code)
{
  switch (code) {
    case RPC_OK:
      return "Ok";
    case RPC_INVALID_ARGUMENT:
      return "Invalid argument";
    case RPC_CORRUPTED_DATA:
      return "Corrupted data";
    case RPC_OUT_OF_MEMORY:
      return "Running out of memory";
    case RPC_CAN_NOT_CONNECT:
      return "Can not connect to remote server";
    case RPC_NO_ACCESS:
      return "No access";
    case RPC_NOT_SUPPORTED:
      return "Not supported";
    case RPC_TOO_MANY_OPEN_FILE:
      return "Too many open files";
    case RPC_INSUFFICIENT_RESOURCE:
      return "Insufficient resource";
    case RPC_INTERNAL_ERROR:
      return "Internal error";
    case RPC_ILLEGAL_STATE:
      return "Illegal state";
    case RPC_TIMEOUT:
      return "Socket timeout";
    default:
      return "Unknown Error";
  }
}

class RPCClientImpl
{
private:
  struct EventListenerHandle
  {
    EventListener listener;
    RPCOpaqueData userData;
    int32_t mask;
    int32_t reserved;
    EventListenerHandle(EventListener l = NULL, RPCOpaqueData u = NULL, int32_t m = 0) : listener(l), userData(u), mask(m), reserved(0) { }
  };

  struct BufferSinkArgument
  {
    void *buffer;
    size_t offset;
  };

  template<typename T>
  struct PrimitiveSinkArgument
  {
    T *result;
    size_t offset;
  };

  struct FirePayloadEventSinkArgument
  {
    RPCClientImpl *client;
    bool commit;
    int32_t dispatched;
    int32_t pending;
    int32_t status;
  };

  typedef void (*ReadDataSink)(void *source, size_t size, void *argument);
  typedef int (*GetSocketAddress)(int socket, struct sockaddr *addr, socklen_t *addrLen);

private:
  enum {
    NEGOTIATION_FLAG_ACCEPT_ZLIB = 1,
    PACKET_FLAG_HEADER_ZLIB_ENCODED = 1,
  };
  enum {
    PACKET_TYPE_REQUEST = 0,
    PACKET_TYPE_RESPONSE = 1,
    PACKET_TYPE_HINT = 2,
  };

  enum {
    HINT_CODE_KEEPALIVE = 0,
  };

  enum {
    STATE_NEGOTIATION_MAGIC,
    STATE_NEGOTIATION_FLAGS,
    STATE_NEGOTIATION_PEER_ID_SIZE,
    STATE_NEGOTIATION_PEER_ID,
    STATE_PACKET_ID,
    STATE_PACKET_CODE,
    STATE_PACKET_FLAGS,
    STATE_PACKET_HEADER_SIZE,
    STATE_PACKET_PAYLOAD_SIZE,
    STATE_PACKET_HEADER,
    STATE_PACKET_PAYLOAD,
    STATE_START_OVER,
  };

private:
  RPCClient *wrapper_;
  mutable int32_t fd_;
  mutable int32_t refcount_;
  int64_t timeout_;
  int64_t keepalive_;
  mutable pthread_mutex_t mutex_;
  pthread_t dispatcherThreadId_;
  KeyValuePairs<int32_t, EventListenerHandle> listeners_;
  mutable int32_t pipe_[2];
  pollfd fdset_[2];
  bool running_;
  mutable pthread_mutex_t stateMutex_;
  int32_t state_;
  mutable int64_t currentId_;

  ChainedBuffer *writeBuffer_;
  ChainedBuffer *readBuffer_;
  ChainedBuffer **nextBuffer_;
  size_t readAvailable_;
  size_t readBufferOffset_;
  mutable pthread_mutex_t pendingMutex_;
  mutable ChainedBuffer *pendingBuffer_;
  mutable ChainedBuffer *currentPendingBuffer_;
  mutable ChainedBuffer **nextPendingBuffer_;
  mutable int32_t numPendingBuffers_;

  /* negotiation */
  bool littleEndian_;
  int32_t version_;
  int32_t flags_;
  int32_t peerIdSize_;
  StringLite peerId_;
  StringLite localId_;

  /* request */
  mutable int64_t requestId_;
  int32_t requestCode_;
  int32_t requestFlags_;
  int32_t requestType_;
  int32_t requestHeaderSize_;
  int32_t requestPayloadSize_;
  int32_t dispatchedPayloadSize_;
  KeyValuePairs<StringLite, StringLite> requestHeaders_;

  static int32_t emptyData_;

private:
  int32_t sendNegotiation();
  int32_t initMultiThread();
  int32_t initBuffer();
  int32_t initLocalId();

  static void lock(pthread_mutex_t *mutex);
  static void unlock(pthread_mutex_t *mutex);

  void lock() const { lock(&mutex_); }
  void unlock() const { unlock(&mutex_); }

  void lockPending() const { lock(&pendingMutex_); }
  void unlockPending() const { unlock(&pendingMutex_); }

  void lockState() const { lock(&stateMutex_); }
  void unlockState() const { unlock(&stateMutex_); }

  bool releaseLocked() const;
  void addRefLocked() const;
  int64_t nextRequestId() const;

  void setState(int32_t state) { lockState(); state_ = state; unlockState(); }
  int32_t getState() const;

  bool isRunningLocked() const { return running_; }
  bool isRunning() const;

  void setRunningLocked(bool running) { running_ = running; }
  void setRunning(bool running);

  static void readToBufferSink(void *data, size_t size, void *userData);
  static void readToStringSink(void *data, size_t size, void *userData);
  template<typename T>
  static void readPrimitiveSink(void *data, size_t size, void *userData);
  static void firePayloadEventSink(void *data, size_t size, void *userData);

  int32_t doRead(size_t size, ReadDataSink sink, void *sinkUserData, int32_t nextState);

  int32_t doRead(int32_t *result, int32_t nextState) { PrimitiveSinkArgument<int32_t> argument = {result, 0}; return doRead(sizeof(int32_t), readPrimitiveSink<int32_t>, &argument, nextState); }
  int32_t doRead(int64_t *result, int32_t nextState) { PrimitiveSinkArgument<int64_t> argument = {result, 0}; return doRead(sizeof(int64_t), readPrimitiveSink<int64_t>, &argument, nextState); }
  int32_t doRead(int32_t size, void *buffer, int32_t nextState) { BufferSinkArgument argument = {buffer, 0}; return doRead(size, readToBufferSink, &argument, nextState); }
  int32_t doRead(int32_t size, StringLite *string, int32_t nextState)
  {
    int32_t st = doRead(size + 1, readToStringSink, string, nextState);
    if (st == RPC_OK) {
      string->resize(size);
    }
    return st;
  }
  int32_t doReadNegotiationMagic();
  int32_t doReadNegotiationFlags();
  int32_t doReadNegotiationPeerIdSize() { return doRead(&peerIdSize_, STATE_NEGOTIATION_PEER_ID); }
  int32_t doReadNegotiationPeerId();
  int32_t doReadPacketId() { return doRead(&requestId_, STATE_PACKET_CODE); }
  int32_t doReadPacketCode() { return doRead(&requestCode_, STATE_PACKET_FLAGS); }
  int32_t doReadPacketFlags()
  {
    int32_t status = doRead(&requestFlags_, STATE_PACKET_HEADER_SIZE);

    if (state_ == STATE_PACKET_HEADER_SIZE) {
      requestType_ = requestFlags_ & 0xFF;
      requestFlags_ >>= 8;
    }

    return status;
  }
  int32_t doReadPacketHeaderSize() { return doRead(&requestHeaderSize_, STATE_PACKET_PAYLOAD_SIZE); }
  int32_t doReadPacketPayloadSize();
  int32_t doReadPacketHeader();
  int32_t doReadPacketPayload();
  int32_t doStartOver();
  int32_t doReadPlainPacketHeader();
  int32_t doReadCompressedPacketHeader();

  int32_t doRead();

  int32_t fillBuffer();

  ChainedBuffer *checkAndGetBuffer();

  void dispatchHint(PacketEventData *data);

  int32_t doFireEvent(int32_t eventType, RPCOpaqueData eventData) const;
  int32_t fireConnectedEvent() const { return doFireEvent(EVENT_TYPE_CONNECTED, NULL); }
  int32_t fireEstablishedEvent() const { return doFireEvent(EVENT_TYPE_ESTABLISHED, NULL); }
  int32_t fireDisconnectedEvent() const { return doFireEvent(EVENT_TYPE_DISCONNECTED, NULL); }
  int32_t firePacketEvent();
  int32_t firePayloadEvent(size_t size, const char *payload, bool commit);
  int32_t fireErrorEvent(int32_t status, const char *message) const;
  int32_t doSendPacket(int64_t id, int32_t code, int32_t type, const KeyValuePairs<StringLite, StringLite> *headers,
      const void *payload, size_t payloadSize) const
  {
    if (getState() <= STATE_NEGOTIATION_PEER_ID) {
      return processErrorHook(RPC_ILLEGAL_STATE);
    }
    if (isDispatcherThread()) {
      return doSendPacketDispatcherThread(id, code, type, headers, payload, payloadSize);
    } else {
      return doSendPacketOtherThread(id, code, type, headers, payload, payloadSize);
    }
  }
  int32_t doSendPacketDispatcherThread(int64_t id, int32_t code, int32_t type,
      const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;
  int32_t doSendPacketOtherThread(int64_t id, int32_t code, int32_t type,
      const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;

  int32_t getAddress(GetSocketAddress get, StringLite *localAddress, uint16_t *port) const;

  void clearDispatcherThreadId()
  {
    memset(&dispatcherThreadId_, 0, sizeof(dispatcherThreadId_));
  }

  void updateDispatcherThreadId()
  {
    dispatcherThreadId_ = pthread_self();
  }

  bool isDispatcherThread() const
  {
    lock();
    bool result = pthread_equal(pthread_self(), dispatcherThreadId_);
    unlock();
    return result;
  }
  bool isDispatcherThreadLocked() const
  {
    return pthread_equal(pthread_self(), dispatcherThreadId_);
  }

  int32_t serialize(int64_t id, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const StringLite &data, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const KeyValuePairs<StringLite, StringLite> &pairs, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t deserialize(bool swap, void *current, size_t size, KeyValuePairs<StringLite, StringLite> *pairs);
  int32_t deserialize(bool swap, void *current, size_t *available, StringLite *data);

  void transferToPending(const iovec *iov, size_t iovsize) const;

  void notifyDispatcherThread() const;
  int32_t processErrorHook(int32_t status) const;
  int32_t convertError() const;
  int32_t doWriteFully(int fd, iovec *iov, size_t *iovsize) const;
  int32_t writeFully(int fd, iovec *iov, size_t *iovsize) const;

  static void checkAndClose(int32_t *fd);

public:
  RPCClientImpl();
  ~RPCClientImpl();

  int32_t init(RPCClient *client, int64_t timeout, int64_t keepalive, int32_t flags);
  int32_t connect(const char *address);
  int32_t addListener(EventListener listener, RPCOpaqueData userData, int32_t eventMask);
  int32_t removeListener(EventListener listener);
  int32_t loop(int32_t flags);
  int32_t breakLoop();
  void close() const;
  int32_t keepalive() const
  {
    return doSendPacket(nextRequestId(), HINT_CODE_KEEPALIVE, PACKET_TYPE_HINT, NULL, NULL, 0);
  }
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
  {
    return doSendPacket(id, code, PACKET_TYPE_RESPONSE, headers, payload, payloadSize);
  }

  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
  {
    *id = nextRequestId();
    return doSendPacket(*id, code, PACKET_TYPE_REQUEST, headers, payload, payloadSize);
  }

  int32_t localAddress(StringLite *localAddress, uint16_t *port) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t checkAndSendPendingPackets();
  void addRef() const;
  bool release() const;
};

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

int32_t RPCClientImpl::convertError() const
{
  int32_t st;
  switch (errno) {
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
        st = convertError();
        break;
      }
    } else if (wrote > 0) {
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
      return code;
    }
    if (MOCA_RPC_FAILED(code = serialize(pair->value, head, current, nextBuffer, totalSize, numBuffers))) {
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
      break;
    }
    if (MOCA_RPC_FAILED(code = deserialize(swap, unsafeGet<void>(buffer, size - available), &available, &value))) {
      break;
    }
    pairs->append(key, value);
  }
  return code;
}

RPCClientImpl::RPCClientImpl() : wrapper_(NULL), fd_(-1), refcount_(1), timeout_(INT64_MAX), keepalive_(INT64_MAX),
  running_(false), state_(STATE_NEGOTIATION_MAGIC), currentId_(0), writeBuffer_(NULL), readBuffer_(NULL),
  nextBuffer_(&readBuffer_), readAvailable_(0), readBufferOffset_(0), pendingBuffer_(NULL),
  currentPendingBuffer_(NULL), nextPendingBuffer_(&pendingBuffer_), numPendingBuffers_(0), littleEndian_(0),
  version_(0), flags_(NEGOTIATION_FLAG_ACCEPT_ZLIB << 8), peerIdSize_(0), requestId_(0), requestFlags_(0), requestType_(0),
  requestHeaderSize_(0), requestPayloadSize_(0), dispatchedPayloadSize_(0)
{
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
  ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers, MOCA_ALIGN(payloadSize + sizeof(Header), 4096));

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
    st = convertError();
    goto cleanupExit;
  }
  if (pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE)) {
    st = convertError();
    goto cleanupExit;
  }
  if (pthread_mutex_init(&mutex_, &mutexattr)) {
    st = convertError();
  }
  if (pthread_mutex_init(&pendingMutex_, NULL)) {
    st = convertError();
  }
  if (pthread_mutex_init(&stateMutex_, NULL)) {
    st = convertError();
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
  while (*ptr && *ptr != ':') {
    ++ptr;
  }
  if (*ptr == '\n') {
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
    return processErrorHook(RPC_INVALID_ARGUMENT);
  }
  struct timeval realTimeout = {static_cast<time_t>(timeout_ / 1000000), static_cast<suseconds_t>(timeout_ % 1000000)};
  const int32_t enabled = 1;
  time_t deadline = getDeadline(timeout_);
  st = RPC_OK;
  for (result = tmp; result; result = result->ai_next) {
    if (isTimeout(deadline)) {
      st = RPC_TIMEOUT;
      goto cleanupExit;
    }
    fd_ = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd_ < 0) {
      continue;
    }
    if (setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &realTimeout, sizeof(realTimeout)) ||
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &realTimeout, sizeof(realTimeout)) ||
        setsockopt(fd_, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) ||
#ifdef SO_NOSIGPIPE
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) ||
#endif
        setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled))) {
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if (::connect(fd_, result->ai_addr, result->ai_addrlen) < 0) {
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if (fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK) == -1) {
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
    fireConnectedEvent();
    st = sendNegotiation();
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
      if (done) {
        break;
      }
    } else if (rd == 0) {
      return processErrorHook(RPC_DISCONNECTED);
    } else {
      if (errno == EINTR) {
        continue;
      } else {
        return convertError();
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
    return rc;
  }
  if (MOCA_RPC_FAILED(rc = initMultiThread())) {
    return rc;
  }
  if (MOCA_RPC_FAILED(rc = initLocalId())) {
    return rc;
  }
  if (pipe(pipe_)) {
    return convertError();
  }
  if (fcntl(pipe_[0], F_SETFL, fcntl(pipe_[0], F_GETFL, 0) | O_NONBLOCK) == -1) {
    return convertError();
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
  totalSize = iovsize;
  st = writeFully(fd_, iov, &iovsize);
  if (totalSize != iovsize) {
    buffer = pendingBuffer_;
    for (size_t idx = 0; idx < iovsize; ++idx) {
      buffer = buffer->next;
    }
    buffer->size = iov[iovsize].iov_len;
    memmove(buffer->buffer, iov[iovsize].iov_base, buffer->size);
  } else {
    nextPendingBuffer_ = &pendingBuffer_;
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
      st = convertError();
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
    return convertError();
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
RPCClient::create(int64_t timeout, int64_t keepalive, int32_t flags)
{
  RPCClientImpl* impl = new RPCClientImpl;
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

#ifdef MOCA_RPC_HAS_STL
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
