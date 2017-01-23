#include "RPCChannel.h"
#include "RPCClientChannel.h"
#include "RPCServerChannel.h"
#include "RPCDispatcher.h"
#include <algorithm>
#include <assert.h>

BEGIN_MOCA_RPC_NAMESPACE

RPCChannel::Builder *
RPCChannel::newBuilder()
{
  return new Builder(new RPCChannelBuilder);
}

RPCChannel::RPCChannel(RPCChannelImpl *impl) : impl_(impl)
{
}

RPCChannel::~RPCChannel()
{
}

RPCChannel::Builder::Builder(RPCChannelBuilder *impl) : impl_(impl)
{
}

RPCChannel::Builder::~Builder()
{
  delete impl_;
}

RPCChannel::Builder *
RPCChannel::Builder::bind(const char *address)
{
  impl_->bind(address);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::connect(const char *address)
{
  impl_->connect(address);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::timeout(int64_t timeout)
{
  impl_->timeout(timeout);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::limit(int32_t size)
{
  impl_->limit(size);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::listener(RPCEventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  impl_->listener(listener, userData, eventMask);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::keepalive(int64_t interval)
{
  impl_->keepalive(interval);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::id(const char *id)
{
  impl_->id(id);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  impl_->logger(logger, level, userData);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::dispatcher(RPCDispatcher *dispatcher)
{
  impl_->dispatcher(dispatcher);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  impl_->attachment(attachment, attachmentDestructor);
  return this;
}

RPCChannel::Builder *
RPCChannel::Builder::flags(int32_t flags)
{
  impl_->flags(flags);
  return this;
}

RPCChannel *
RPCChannel::Builder::build()
{
  return impl_->build();
}

RPCChannelBuilder::RPCChannelBuilder() : isClient_(true), listener_(NULL),
  listenerUserData_(NULL), listenerMask_(0), limit_(4 * 1024 * 1024), flags_(0),
  timeout_(0x7FFFFFFFFFFFFFFFL), keepaliveInterval_(0x7FFFFFFFFFFFFFFFL),
  logger_(defaultRPCLogger), level_(defaultRPCLoggerLevel),
  loggerUserData_(defaultRPCLoggerUserData), dispatcher_(NULL),
  attachment_(NULL), attachmentDestructor_(NULL)
{
  uuidGenerate(&id_);
}

RPCChannelBuilder::~RPCChannelBuilder()
{
}

void
RPCChannelBuilder::address(const char *address, bool isClient)
{
  address_.assign(address);
  isClient_ = isClient;
}

void
RPCChannelBuilder::bind(const char *address)
{
  this->address(address, false);
}

void
RPCChannelBuilder::connect(const char *address)
{
  this->address(address, true);
}

void
RPCChannelBuilder::timeout(int64_t timeout)
{
  timeout_ = timeout;
}

void
RPCChannelBuilder::limit(int32_t size)
{
  limit_ = size;
}

void
RPCChannelBuilder::listener(RPCEventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  listener_ = listener;
  listenerUserData_ = userData;
  listenerMask_ = eventMask;
}

void
RPCChannelBuilder::keepalive(int64_t interval)
{
  keepaliveInterval_ = interval;
}

void
RPCChannelBuilder::id(const char *id)
{
  id_.assign(id);
}

void
RPCChannelBuilder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  logger_ = logger;
  level_ = level;
  loggerUserData_ = userData;
}

void
RPCChannelBuilder::dispatcher(RPCDispatcher *dispatcher)
{
  if (dispatcher_) {
    dispatcher_->release();
  }
  dispatcher_ = dispatcher;
  dispatcher_->addRef();
}

void
RPCChannelBuilder::flags(int32_t flags)
{
  flags_ = flags;
}

void
RPCChannelBuilder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  destroyAttachment();
  attachment_ = attachment;
  attachmentDestructor_ = attachmentDestructor;
}

void
RPCChannelBuilder::destroyAttachment()
{
  if (attachment_ && attachmentDestructor_) {
    attachmentDestructor_(attachment_);
  }
  attachment_ = NULL;
  attachmentDestructor_ = NULL;
}

RPCChannel *
RPCChannelBuilder::build()
{
  RPCChannelImpl *channel;
  if (isClient_) {
    channel = new RPCClientChannel;
  } else {
    channel = new RPCServerChannel;
  }
  RPCChannel *wrapper = channel->wrap();
  int32_t st = channel->init(flags_, dispatcher_, id_, address_, listener_, listenerUserData_, listenerMask_,
      timeout_, limit_, keepaliveInterval_, logger_, level_, loggerUserData_, attachment_, attachmentDestructor_);
  attachment_ = NULL;
  attachmentDestructor_ = NULL;
  dispatcher_->release();
  uuidGenerate(&id_);
  if (st) {
    channel->close();
    channel->release();
    return NULL;
  } else {
    return wrapper;
  }
}

RPCChannelImpl::RPCChannelImpl() : timerInitialized_(false), keepaliveEnabled_(false),
  timeoutEnabled_(false), localPort_(0), remotePort_(0), key_(0), channelFlags_(0),
  limit_(0), listenerMask_(0), asyncStatus_(0), wrapper_(NULL), eventLoop_(NULL),
  timeout_(0), keepaliveInterval_(0), listener_(NULL), listenerUserData_(NULL),
  dispatcher_(NULL), channelState_(UNINITIALIZED), attachment_(NULL), attachmentDestructor_(NULL)
{
  memset(&address_, 0, sizeof(address_));
  memset(&handle_, 0, sizeof(handle_));
  memset(&remoteSockAddress_, 0, sizeof(remoteSockAddress_));
  key_ = rand();
}

RPCChannelImpl::~RPCChannelImpl()
{
  fireDestroyedEvent();
  destroyAttachment();
  delete wrapper_;
  if (dispatcher_) {
    dispatcher_->release();
  }
}

bool
RPCChannelImpl::isDispatchingThread() const
{
  return dispatcher_->isDispatchingThread();
}

void
RPCChannelImpl::close()
{
  doClose(dispatcher_->isDispatchingThread());
}

void
RPCChannelImpl::finishAsync(int32_t st)
{
  asyncToken_.finish(st);
}

void
RPCChannelImpl::onClose()
{
  finishAsync(0);
  fireDisconnectedEvent();
  release();
}

void
RPCChannelImpl::onClose(uv_handle_t *handle)
{
  RPCChannelImpl *channel = static_cast<RPCChannelImpl *>(handle->data);
  if (handle == reinterpret_cast<uv_handle_t *>(&channel->timer_)) {
    uv_close(reinterpret_cast<uv_handle_t *>(&channel->handle_), onClose);
  } else {
    channel->onClose();
  }
}

void
RPCChannelImpl::onAsyncClose()
{
  assert(dispatcher_->isDispatchingThread());
  if (timerInitialized_) {
    uv_timer_stop(&timer_);
    uv_close(reinterpret_cast<uv_handle_t *>(&timer_), onClose);
  } else {
    uv_close(reinterpret_cast<uv_handle_t *>(&handle_), onClose);
  }
}

void
RPCChannelImpl::onAsyncClose(RPCOpaqueData data)
{
  return static_cast<RPCChannelImpl *>(data)->onAsyncClose();
}

void
RPCChannelImpl::doClose(bool dispatchingThread)
{
  int32_t state;
  while (true) {
    state = channelState_.get();
    if (state < INITIALIZING_RESOLVE || state >= CLOSING) {
      return;
    }
    if (channelState_.compareAndSet(state, CLOSING)) {
      break;
    }
  }
  RPC_LOG_INFO("Closing connection between %s:%u and %s:%u",
      localAddress_.str(), static_cast<uint32_t>(localPort_),
      remoteAddress_.str(), static_cast<uint32_t>(remotePort_));
  addRef();
  if (!dispatchingThread) {
    asyncToken_.start();
    addRef();
    FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher_)->submitAsync(onAsyncClose, this, key());
    asyncToken_.wait();
    release();
  } else {
    onAsyncClose();
  }
}

int32_t
RPCChannelImpl::initSocket(int32_t domain)
{
  int32_t st;
  channelState_.set(INITIALIZING_SOCKET);
  assert(dispatcher_->isDispatchingThread());
  if ((st = uv_tcp_init_ex(eventLoop_, &handle_, domain)) ||
      (st = uv_tcp_nodelay(&handle_, 1))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
  } else {
    handle_.data = this;
  }
  uv_tcp_keepalive(&handle_, 1, keepaliveInterval_ > 1000000 ? keepaliveInterval_ / 1000000 : 120);
  return st;
}

void
RPCChannelImpl::onResolved(int32_t st, struct addrinfo *res)
{
  assert(dispatcher_->isDispatchingThread());
  if (st != 0) {
    RPC_LOG_ERROR("Could not resolve host address. %s", uv_err_name(st));
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
    goto cleanupExit;
  }

  if (MOCA_RPC_FAILED(st = initSocket(res->ai_addr->sa_family))) {
    goto cleanupExit;
  }

  if (MOCA_RPC_FAILED(st = onResolved(res))) {
    goto cleanupExit;
  }

  if (isKeepaliveEnabled() || isTimeoutEnabled()) {
    uv_timer_init(loop(), &timer_);
    timer_.data = this;
    int64_t interval = min<>(timeout_, keepaliveInterval_);
    /* real timer interval is only valid within 1 and 60 seconds*/
    if (interval < 4000) {
      interval = 4000;
    } else if (interval > 240000) {
      interval = 240000;
    }
    interval /= 4;
    uv_timer_start(&timer_, onTimer, interval, interval);
    timerInitialized_ = true;
  }

  channelState_.set(INITIALIZED);

cleanupExit:
  release();
  if (res) {
    uv_freeaddrinfo(res);
  }
}

void
RPCChannelImpl::onResolved(uv_getaddrinfo_t *resolver, int32_t st, struct addrinfo *res)
{
  static_cast<RPCChannelImpl *>(resolver->data)->onResolved(st, res);
}

void
RPCChannelImpl::onResolve()
{
  int32_t st;
  struct addrinfo hints;
  hints.ai_family = PF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = 0;
  const char *ptr = address_.str();
  const char *host;
  const char *port;

  while (*ptr && *ptr != ':') {
    ++ptr;
  }
  if (*ptr == '\0') {
    RPC_LOG_ERROR("Invalid address %s", address_.str());
    asyncToken_.finish(RPC_INVALID_ARGUMENT);
    return;
  }
  host = address_.str();
  port = ptr;
  *const_cast<char *>(port) = '\0';
  ++port;

  resolver_.data = this;
  assert(dispatcher_->isDispatchingThread());
  channelState_.set(INITIALIZING_RESOLVE);
  if ((st = uv_getaddrinfo(eventLoop_, &resolver_, onResolved, host, port, &hints))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
    asyncToken_.finish(st);
    this->release();
  }
  return;
}

void
RPCChannelImpl::onAsyncResolve(RPCOpaqueData data)
{
  static_cast<RPCChannelImpl *>(data)->onResolve();
}

int32_t
RPCChannelImpl::start()
{
  addRef();
  if (!dispatcher_->isDispatchingThread() && isBlocking()) {
    asyncToken_.start();
    addRef();
  }

  int32_t st;
  if ((st = FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher_)->submitAsync(onAsyncResolve, this, key()))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
    release();
  } else {
    if (!dispatcher_->isDispatchingThread() && isBlocking()) {
      st = asyncToken_.wait();
      release();
    }
  }
  return st;
}

int32_t
RPCChannelImpl::init(int32_t flags, RPCDispatcher *dispatcher, const StringLite &id, const StringLite &address,
    RPCEventListener listener, RPCOpaqueData listenerUserData, int32_t listenerMask, int64_t timeout,
    int32_t limit, int64_t keepaliveInterval, RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData,
    RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  uuidGenerate(&localId_);
  channelFlags_ = flags;
  dispatcher_ = dispatcher;
  dispatcher_->addRef();
  localId_ = id;
  listener_ = listener;
  listenerUserData_ = listenerUserData;
  listenerMask_ = listenerMask;
  timeoutEnabled_ = timeout != 0x7FFFFFFFFFFFFFFFL;
  timeout_ = timeout / 1000;
  limit_ = limit;
  keepaliveEnabled_ = keepaliveInterval != 0x7FFFFFFFFFFFFFFFL;
  keepaliveInterval_ = keepaliveInterval / 1000;
  logger_ = logger;
  level_ = level;
  loggerUserData_ = userData;
  address_ = address;
  attachment_ = attachment;
  attachmentDestructor_ = attachmentDestructor;
  int32_t st;
  eventLoop_ = FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher)->loop();
  channelState_.set(INITIALIZING_MULTITHREAD);
  if ((st = asyncToken_.init())) {
    processErrorHook(st);
    return st;
  }

  fireCreatedEvent();
  return start();
}

int32_t
RPCChannelImpl::doFireEvent(int32_t eventType, RPCOpaqueData eventData) const
{
  if (listenerMask_ & eventType) {
    listener_(wrapper_, eventType, eventData, listenerUserData_);
  }

  return RPC_OK;
}

int32_t
RPCChannelImpl::processErrorHook(int32_t status) const
{
  switch (status) {
    case RPC_DISCONNECTED:
    case RPC_CAN_NOT_CONNECT:
    case RPC_INCOMPATIBLE_PROTOCOL:
      const_cast<RPCChannelImpl *>(this)->close();
      break;
    default:
      fireErrorEvent(status, NULL);
      break;
  }
  return status;
}

int32_t
RPCChannelImpl::fireErrorEvent(int32_t status, const char *message) const
{
  if (message == NULL) {
    message = errorString(status);
  }
  ErrorEventData data = { status, message };
  return doFireEvent(EVENT_TYPE_CHANNEL_ERROR, &data);
}

int32_t
RPCChannelImpl::getAddress(GetSocketAddress get, sockaddr_storage *address, int32_t *size) const
{
  int32_t st = get(&handle_, reinterpret_cast<sockaddr *>(address), size);
  if (st != 0) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    return processErrorHook(st);
  }
  return st;
}

int32_t
RPCChannelImpl::getAddress(GetSocketAddress get, StringLite *address, uint16_t *port) const
{
  sockaddr_storage addr;
  int32_t size = sizeof(addr);

  int32_t st = getAddress(get, &addr, &size);
  if (st != 0) {
    return st;
  }

  return getInetAddressPresentation(&addr, address, port);
}

void
RPCChannelImpl::inherit(RPCChannelImpl *client)
{
  client->channelFlags_ = channelFlags_;
  client->dispatcher_ = dispatcher_;
  dispatcher_->addRef();
  client->eventLoop_ = eventLoop_;
  client->timeout_ = timeout_;
  client->limit_ = limit_;
  client->keepaliveInterval_ = keepaliveInterval_;
  client->listener_ = listener_;
  client->listenerUserData_ = listenerUserData_;
  client->listenerMask_ = listenerMask_;
  client->logger_ = logger_;
  client->level_ = level_;
  client->loggerUserData_ = loggerUserData_;
  client->localAddress_ = localAddress_;
  client->localPort_ = localPort_;
  client->localId_ = localId_;
}

void
RPCChannelImpl::onTimer(uint64_t now)
{
}

void
RPCChannelImpl::onTimer(uv_timer_t *handle)
{
  return static_cast<RPCChannelImpl *>(handle->data)->onTimer(uv_now(handle->loop));
}

int32_t
RPCChannel::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return impl_->localAddress(localAddress, port);
}

int32_t
RPCChannel::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return impl_->remoteAddress(remoteAddress, port);
}

int32_t
RPCChannel::localId(StringLite *localId) const
{
  return impl_->localId(localId);
}

int32_t
RPCChannel::remoteId(StringLite *remoteId) const
{
  return impl_->remoteId(remoteId);
}

int32_t
RPCChannel::response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  return client->response(id, code, headers, payload, payloadSize);
}

int32_t
RPCChannel::request(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  return client->request(id, code, headers, payload, payloadSize);
}

int32_t
RPCChannel::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  return client->request(id, code, headers, payload, payloadSize);
}

void
RPCChannel::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor)
{
  impl_->attachment(attachment, destructor);
}

RPCOpaqueData
RPCChannel::attachment() const
{
  return impl_->attachment();
}

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
void convert(const KeyValueMap *from, KeyValuePairs<StringLite, StringLite> *to);

int32_t
RPCChannel::response(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return client->response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return client->response(id, code, NULL, payload, payloadSize);
  }
}

int32_t
RPCChannel::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return client->request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return client->request(id, code, NULL, payload, payloadSize);
  }
}

int32_t
RPCChannel::request(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  RPCClientChannel *client = static_cast<RPCClientChannel *>(impl_);
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return client->request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return client->request(id, code, NULL, payload, payloadSize);
  }
}
#endif

void
RPCChannel::addRef()
{
  impl_->addRef();
}

bool
RPCChannel::release()
{
  return impl_->release();
}

void
RPCChannel::close()
{
  return impl_->close();
}

END_MOCA_RPC_NAMESPACE
