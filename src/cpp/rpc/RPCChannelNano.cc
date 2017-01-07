#include "RPCChannelNano.h"
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
#include <stdarg.h>
#include <sys/time.h>
#include "RPCLogging.h"
#ifndef IOV_MAX
#define IOV_MAX (8)
#endif

BEGIN_MOCA_RPC_NAMESPACE

RPCChannelNano::RPCChannelNano(RPCChannelNanoImpl *impl) : impl_(impl)
{
}

RPCChannelNano::~RPCChannelNano()
{
}

int32_t
RPCChannelNano::loop(int32_t flags)
{
  return impl_->loop(flags);
}

int32_t
RPCChannelNano::breakLoop() const
{
  return impl_->breakLoop();
}

int32_t
RPCChannelNano::response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->response(id, code, headers, payload, payloadSize);
}

int32_t
RPCChannelNano::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize);
}

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
void convert(const KeyValueMap *from, KeyValuePairs<StringLite, StringLite> *to);

int32_t
RPCChannelNano::response(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return impl_->response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return impl_->response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t
RPCChannelNano::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return impl_->request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return impl_->request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}
#endif

int32_t
RPCChannelNano::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return impl_->localAddress(localAddress, port);
}

int32_t
RPCChannelNano::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return impl_->remoteAddress(remoteAddress, port);
}

int32_t
RPCChannelNano::localId(StringLite *localId) const
{
  return impl_->localId(localId);
}

int32_t
RPCChannelNano::remoteId(StringLite *remoteId) const
{
  return impl_->remoteId(remoteId);
}

void
RPCChannelNano::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor)
{
  impl_->attachment(attachment, destructor);
}

RPCOpaqueData
RPCChannelNano::attachment() const
{
  return impl_->attachment();
}

void
RPCChannelNano::addRef()
{
  impl_->addRef();
}

bool
RPCChannelNano::release()
{
  bool released = impl_->release();
  if (released) {
    delete this;
  }
  return released;
}

int32_t
RPCChannelNano::close()
{
  impl_->close();
  return RPC_OK;
}

int32_t
RPCChannelNano::keepalive() const
{
  return impl_->keepalive();
}

int32_t RPCChannelNanoImpl::emptyData_ = 0;

int32_t RPCChannelNanoImpl::convertError(const char *func, const char *file, uint32_t line) const
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
    case EINTR:
      return RPC_WOULDBLOCK;
    default:
      st = RPC_INTERNAL_ERROR;
      break;
  }

  char buffer[128];
  RPC_LOG_ERROR("System error from [%s|%s:%u] . %d:%s", func, file, line, code, strerror_r(code, buffer, sizeof(buffer)) == 0 ? buffer : "UNKNOWN ERROR");
  return protocol_.processErrorHook(st);
}

int32_t
RPCChannelNanoImpl::doWriteFully(int fd, iovec *iov, size_t *iovsize) const
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
RPCChannelNanoImpl::writeFully(int fd, iovec *iov, size_t *iovsize) const
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

RPCChannelNanoImpl::RPCChannelNanoImpl(RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData,
    RPCChannelNano::EventListener listener, RPCOpaqueData listenerUserData, int32_t eventMask,
    RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
  : protocol_(this, closeChannel, protocolEventListener, this, protocolWriteSink, this, 0, 512),
  wrapper_(NULL), fd_(-1), timeout_(INT64_MAX), keepalive_(INT64_MAX), listener_(listener), listenerMask_(eventMask),
  listenerUserData_(listenerUserData), running_(false), currentId_(0), pendingBuffer_(NULL),
  currentPendingBuffer_(NULL), nextPendingBuffer_(&pendingBuffer_), numPendingBuffers_(0),
  logger_(logger), level_(level), loggerUserData_(loggerUserData),
  attachment_(attachment), attachmentDestructor_(attachmentDestructor)
{
  pipe_[0] = -1;
  pipe_[1] = -1;
  clearDispatcherThreadId();
}

RPCChannelNanoImpl::~RPCChannelNanoImpl()
{
  RPC_LOG_INFO("Destroying rpc channel at %p", this);
  close();
  ChainedBuffer::destroy(&pendingBuffer_);
  pthread_mutex_destroy(&mutex_);
  pthread_mutex_destroy(&pendingMutex_);
}

int32_t
RPCChannelNanoImpl::doFireEvent(int32_t eventType, RPCOpaqueData eventData) const
{
  lock();
  if (listenerMask_ & eventType) {
    listener_(wrapper_, eventType, eventData, listenerUserData_);
  }
  unlock();
  return RPC_OK;
}

void
RPCChannelNanoImpl::checkAndClose(int32_t *fd)
{
  if (*fd > 0) {
    ::close(*fd);
    *fd = -1;
  }
}

void
RPCChannelNanoImpl::close() const
{
  checkAndClose(&fd_);
  checkAndClose(pipe_ + 0);
  checkAndClose(pipe_ + 1);
  fdset_[0].fd = -1;
  fdset_[1].fd = -1;
}

int32_t
RPCChannelNanoImpl::initMultiThread()
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

cleanupExit:
  pthread_mutexattr_destroy(&mutexattr);
  return st;
}

int32_t
RPCChannelNanoImpl::connect(const char *address)
{
  StringLite addressCopy(address);
  const char *ptr = addressCopy.str();
  RPC_LOG_DEBUG("Connecting to %s", address);
  while (*ptr && *ptr != ':') {
    ++ptr;
  }
  if (*ptr == '\0') {
    RPC_LOG_ERROR("Invalid server address %s", address);
    return protocol_.processErrorHook(RPC_INVALID_ARGUMENT);
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
    return protocol_.processErrorHook(RPC_INVALID_ARGUMENT);
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
    if (fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK) == -1) {
      RPC_LOG_ERROR("Could not set socket to nonblocking mode. %d", errno);
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if ((st = ::connect(fd_, result->ai_addr, result->ai_addrlen)) < 0 && errno != EINPROGRESS) {
      RPC_LOG_ERROR("Could not connect to remote server. %d", errno);
      ::close(fd_);
      fd_ = -1;
      continue;
    }
    if (errno == EINPROGRESS) {
      pollfd fd = {fd_, POLLOUT, 0};
      if ((st = poll(&fd, 1, (deadline - time(NULL)) * 1000) == 0)) {
        st = RPC_TIMEOUT;
        ::close(fd_);
        fd_ = -1;
        RPC_LOG_ERROR("Connecting to %s timeout", address);
        goto cleanupExit;
      } else if (st < 0) {
        RPC_LOG_ERROR("Could not connect to remote server. %d", errno);
        ::close(fd_);
        fd_ = -1;
        continue;
      }
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
    RPC_LOG_INFO("Channel %p is connected to %s", this, address);
    protocol_.fireConnectedEvent();
    st = protocol_.sendNegotiation();
    RPC_LOG_INFO("Sent negotiation to %s", address);
  }

cleanupExit:
  if (tmp) {
    freeaddrinfo(tmp);
  }
  if (fd_ < 0) {
    protocol_.processErrorHook(st = RPC_CAN_NOT_CONNECT);
  }
  return st;
}

int32_t
RPCChannelNanoImpl::doRead()
{
  int32_t st = RPC_OK;

  ChainedBuffer *buffer;
  bool done = false;
  size_t total = 0;
  size_t size = 1024;

  while ((buffer = protocol_.checkAndGetBuffer(&size))) {
    int32_t bufferAvailable = buffer->available();
    int32_t rd = read(fd_, buffer->buffer + buffer->size, bufferAvailable);
    if (rd > 0) {
      if (rd < bufferAvailable) {
        /* done with whatever data in tcp buffer */
        done = true;
      }
      buffer->size += rd;
      total += rd;
      RPC_LOG_DEBUG("Read %d bytes data out of tcp buffer", rd);
      if (done) {
        break;
      }
    } else if (rd == 0) {
      st = RPC_DISCONNECTED;
      break;
    } else {
      if (errno == EINTR) {
        continue;
      } else {
        st = convertError(__FUNCTION__, __FILE__, __LINE__);
        break;
      }
    }
    size = 1024;
  }

  if (total > 0) {
    protocol_.onRead(total);
  }
  if (MOCA_RPC_FAILED(st) && st != RPC_WOULDBLOCK) {
    st = protocol_.processErrorHook(st);
  }
  return st;
}

bool
RPCChannelNanoImpl::isRunning() const
{
  lock();
  bool running = isRunningLocked();
  unlock();
  return running;
}

void
RPCChannelNanoImpl::setRunning(bool running)
{
  lock();
  setRunningLocked(running);
  unlock();
  return;
}

int32_t
RPCChannelNanoImpl::init(const StringLite &id, int64_t timeout, int64_t keepalive, int32_t channelFlags, int32_t limit, int32_t protocolFlags)
{
  int32_t rc;
  timeout_ = timeout < SECOND / 1000 ? SECOND / 1000 : timeout;
  keepalive_ = keepalive < SECOND / 100 ? SECOND / 100 : keepalive;
  if (MOCA_RPC_FAILED(rc = initMultiThread())) {
    RPC_LOG_ERROR("Could not initialize threading support. %d", rc);
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
  if (MOCA_RPC_FAILED(rc = protocol_.init(id, logger_, level_, loggerUserData_, protocolFlags, limit, channelFlags))) {
    RPC_LOG_ERROR("Could not initialize rpc protocol. %d", rc);
    return rc;
  }
  return doFireEvent(EVENT_TYPE_CHANNEL_CREATED, NULL);
}

void
RPCChannelNanoImpl::lock(pthread_mutex_t *mutex)
{
  pthread_mutex_lock(mutex);
}

void
RPCChannelNanoImpl::unlock(pthread_mutex_t *mutex)
{
  pthread_mutex_unlock(mutex);
}

int32_t
RPCChannelNanoImpl::checkAndSendPendingPackets()
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
RPCChannelNanoImpl::loop(int32_t flags)
{
  int32_t st = RPC_OK;
  lock();
  int64_t timeout = (flags & RPCChannelNano::LOOP_FLAG_NONBLOCK) ? 0 : timeout_;
  int64_t interval = timeout < keepalive_ ? timeout_ : keepalive_;
  int32_t realTimeout = interval == INT64_MAX ? INT32_MAX : interval / 1000;
  bool keepaliveEnabled = true;
  timeval start = {0, 0};
  bool hasRead = false;
  if (running_) {
    RPC_LOG_ERROR("There is already another thread running loop");
    st = RPC_ILLEGAL_STATE;
    goto cleanupExit;
  }
  if (fdset_[0].fd == -1 || fdset_[1].fd == -1) {
    RPC_LOG_ERROR("Channel is not connected");
    st = RPC_ILLEGAL_STATE;
    goto cleanupExit;
  }
  updateDispatcherThreadId();
  addRef();
  running_ = true;
  unlock();

  if (realTimeout > 0) {
    gettimeofday(&start, NULL);
  }

  while (isRunning() && st == RPC_OK && fdset_[0].fd != -1 && fdset_[1].fd != -1) {
    fdset_[0].revents = 0;
    fdset_[1].revents = 0;
    if (MOCA_RPC_FAILED(st = checkAndSendPendingPackets()) && st != RPC_WOULDBLOCK) {
      break;
    }
    st = poll(fdset_, 2, realTimeout);
    if (st > 0) {
      st = RPC_OK;
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
    } else if (st == 0) {
      if ((flags & RPCChannelNano::LOOP_FLAG_NONBLOCK)) {
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
        } else if (protocol_.isEstablished() && keepaliveEnabled) {
          if ((st = keepalive()) == RPC_CANCELED) {
            keepaliveEnabled = false;
            st = RPC_OK;
          }
        }
      }
    } else {
      if ((st = convertError(__FUNCTION__, __FILE__, __LINE__)) == RPC_WOULDBLOCK) {
        st = RPC_OK;
      }
    }
    if (flags & RPCChannelNano::LOOP_FLAG_ONE_SHOT) {
      break;
    }
  }

  lock();
  clearDispatcherThreadId();
  running_ = false;

cleanupExit:
  release();
  unlock();

  if (st != RPC_OK && st != RPC_WOULDBLOCK) {
    protocol_.processErrorHook(st);
  }
  return st;
}

int32_t
RPCChannelNanoImpl::getAddress(GetSocketAddress get, StringLite *address, uint16_t *port) const
{
  sockaddr_storage addr;
  socklen_t size = sizeof(addr);
  int32_t rc = get(fd_, reinterpret_cast<sockaddr *>(&addr), &size);
  if (rc != 0) {
    return convertError(__FUNCTION__, __FILE__, __LINE__);
  }

  return getInetAddressPresentation(&addr, address, port);
}

int32_t
RPCChannelNanoImpl::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return getAddress(getsockname, localAddress, port);
}

int32_t
RPCChannelNanoImpl::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return getAddress(getpeername, remoteAddress, port);
}

int32_t
RPCChannelNanoImpl::localId(StringLite *localId) const
{
  *localId = protocol_.localId();
  return RPC_OK;
}

int32_t
RPCChannelNanoImpl::remoteId(StringLite *remoteId) const
{
  *remoteId = protocol_.remoteId();
  return RPC_OK;
}

void
RPCChannelNanoImpl::notifyDispatcherThread() const
{
  lock();
  addRef();
  unlock();

  write(pipe_[1], &emptyData_, sizeof(emptyData_));

  release();
}

int32_t
RPCChannelNanoImpl::breakLoop() const
{
  lock();
  addRef();
  if (running_) {
    running_ = false;
  }
  unlock();

  write(pipe_[1], &emptyData_, sizeof(emptyData_));

  release();

  return RPC_OK;
}

RPCChannelNano *
RPCChannelNanoImpl::create(const StringLite &address, const StringLite &id, int64_t timeout, int64_t keepalive,
    int32_t flags, int32_t limit, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData,
    RPCChannelNano::EventListener listener, RPCOpaqueData userData, int32_t eventMask,
    RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor, int32_t protocolFlags)
{
  RPCChannelNanoImpl* impl = new RPCChannelNanoImpl(logger, level, loggerUserData,
      listener, userData, eventMask, attachment, attachmentDestructor);
  RPCChannelNano *channel = impl->wrap();
  int32_t st;
  MOCA_RPC_DO_GOTO(st, impl->init(id, timeout, keepalive, flags, limit, protocolFlags), cleanupExit);
  MOCA_RPC_DO_GOTO(st, impl->connect(address.str()), cleanupExit);
  return channel;
cleanupExit:
  channel->release();
  return NULL;
}

void
RPCChannelNanoImpl::doWrite(ChainedBuffer **buffer)
{
  addRef();
  lockPending();
  ChainedBuffer *head = *buffer;
  *nextPendingBuffer_ = head;
  while (true) {
    ++numPendingBuffers_;
    if (head->next == NULL) {
      nextPendingBuffer_ = &head->next;
      break;
    }
    head = head->next;
  }
  unlockPending();
  this->release();
  *buffer = NULL;
}

void
RPCChannelNanoImpl::protocolEventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  static_cast<RPCChannelNanoImpl *>(userData)->doFireEvent(eventType, eventData);
}

void
RPCChannelNanoImpl::protocolWriteSink(ChainedBuffer **buffer, RPCOpaqueData argument)
{
  static_cast<RPCChannelNanoImpl *>(argument)->doWrite(buffer);
}

RPCChannelNano::Builder *
RPCChannelNano::newBuilder()
{
  return new Builder(new RPCChannelNanoBuilder);
}

RPCChannelNano::Builder::Builder(RPCChannelNanoBuilder *impl) : impl_(impl)
{
}

RPCChannelNano::Builder::~Builder()
{
  delete impl_;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::connect(const char *address)
{
  impl_->connect(address);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::timeout(int64_t timeout)
{
  impl_->timeout(timeout);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::limit(int32_t size)
{
  impl_->limit(size);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::listener(RPCChannelNano::EventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  impl_->listener(listener, userData, eventMask);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::keepalive(int64_t interval)
{
  impl_->keepalive(interval);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::id(const char *id)
{
  impl_->id(id);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  impl_->logger(logger, level, userData);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  impl_->attachment(attachment, attachmentDestructor);
  return this;
}

RPCChannelNano::Builder *
RPCChannelNano::Builder::flags(int32_t flags)
{
  impl_->flags(flags);
  return this;
}

RPCChannelNano *
RPCChannelNano::Builder::build()
{
  return impl_->build();
}

RPCChannelNanoBuilder::RPCChannelNanoBuilder()
  : listener_(NULL), listenerUserData_(NULL), listenerMask_(0), timeout_(0x7FFFFFFFFFFFFFFFL),
  limit_(4 * 1024 * 1024), keepaliveInterval_(0x7FFFFFFFFFFFFFFFL), flags_(0),
  logger_(rpcSimpleLogger), level_(DEFAULT_LOG_LEVEL), loggerUserData_(defaultRPCSimpleLoggerSink),
  attachment_(NULL), attachmentDestructor_(NULL)
{
  uuidGenerate(&id_);
}

RPCChannelNanoBuilder::~RPCChannelNanoBuilder()
{
}

void
RPCChannelNanoBuilder::address(const char *address)
{
  address_.assign(address);
}

void
RPCChannelNanoBuilder::connect(const char *address)
{
  this->address(address);
}

void
RPCChannelNanoBuilder::timeout(int64_t timeout)
{
  timeout_ = timeout;
}

void
RPCChannelNanoBuilder::limit(int32_t size)
{
  limit_ = size;
}

void
RPCChannelNanoBuilder::listener(RPCChannelNano::EventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  listener_ = listener;
  listenerUserData_ = userData;
  listenerMask_ = eventMask;
}

void
RPCChannelNanoBuilder::keepalive(int64_t interval)
{
  keepaliveInterval_ = interval;
}

void
RPCChannelNanoBuilder::id(const char *id)
{
  id_.assign(id);
}

void
RPCChannelNanoBuilder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  if (logger) {
    logger_ = logger;
    level_ = level;
    loggerUserData_ = userData;
  } else {
    logger_ = rpcSimpleLogger;
    level_ = defaultRPCSimpleLoggerLogLevel;
    loggerUserData_ = defaultRPCSimpleLoggerSink;
  }
}

void
RPCChannelNanoBuilder::flags(int32_t flags)
{
  flags_ = (flags & 0xFF) << 16;
}

void
RPCChannelNanoBuilder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  destroyAttachment();
  attachment_ = attachment;
  attachmentDestructor_ = attachmentDestructor;
}

void
RPCChannelNanoBuilder::destroyAttachment()
{
  if (attachment_ && attachmentDestructor_) {
    attachmentDestructor_(attachment_);
  }
  attachment_ = NULL;
  attachmentDestructor_ = NULL;
}

RPCChannelNano *
RPCChannelNanoBuilder::build()
{
  RPCChannelNano *channel = RPCChannelNanoImpl::create(address_, id_, timeout_, keepaliveInterval_, flags_, limit_,
      logger_, level_, loggerUserData_, listener_, listenerUserData_, listenerMask_, attachment_, attachmentDestructor_, 0);
  if (channel) {
    attachment_ = NULL;
    attachmentDestructor_ = NULL;
    uuidGenerate(&id_);
  }
  return channel;
}


END_MOCA_RPC_NAMESPACE
