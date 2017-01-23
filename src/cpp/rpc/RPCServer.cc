#include "RPCServer.h"
#include "RPCChannel.h"
#include "RPCDispatcher.h"

BEGIN_MOCA_RPC_NAMESPACE

RPCServer::RPCServer(RPCServerImpl *impl) : impl_(impl)
{
}

RPCServer::Builder *
RPCServer::newBuilder()
{
  return new Builder(new RPCServerBuilder);
}

RPCServer::Builder::Builder(RPCServerBuilder *impl) : impl_(impl)
{
}

RPCServer::Builder::~Builder()
{
  delete impl_;
}

RPCServer::Builder *
RPCServer::Builder::bind(const char *address)
{
  impl_->bind(address);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::timeout(int64_t timeout)
{
  impl_->timeout(timeout);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::headerLimit(int32_t size)
{
  impl_->headerLimit(size);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::payloadLimit(int32_t size)
{
  impl_->payloadLimit(size);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::listener(RPCServerEventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  impl_->listener(listener, userData, eventMask);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::keepalive(int64_t interval)
{
  impl_->keepalive(interval);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::id(const char *id)
{
  impl_->id(id);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  impl_->logger(logger, level, userData);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::flags(int32_t flags)
{
  impl_->flags(flags);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::dispatcher(RPCDispatcher *dispatcher)
{
  impl_->dispatcher(dispatcher);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  impl_->attachment(attachment, attachmentDestructor);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::threadPoolSize(int32_t size)
{
  impl_->threadPoolSize(size);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::connectionLimitPerIp(int32_t limit)
{
  impl_->connectionLimitPerIp(limit);
  return this;
}
RPCServer::Builder *
RPCServer::Builder::connectRateLimitPerIp(int32_t limit)
{
  impl_->connectRateLimitPerIp(limit);
  return this;
}

RPCServer *
RPCServer::Builder::build()
{
  return impl_->build();
}

RPCServerBuilder::RPCServerBuilder()
  : builder_(RPCChannel::newBuilder()), logger_(defaultRPCLogger),
    level_(defaultRPCLoggerLevel), loggerUserData_(defaultRPCLoggerUserData),
    payloadLimit_(4 * 1024 * 1024), threadPoolSize_(0), dispatcher_(NULL),
    connectionLimitPerIp_(256), connectRateLimitPerIp_(256)
{
}
RPCServerBuilder::~RPCServerBuilder()
{
  if (dispatcher_) {
    dispatcher_->release();
  }
  delete builder_;
}
void
RPCServerBuilder::bind(const char *address)
{
  builder_->bind(address);
}
void
RPCServerBuilder::timeout(int64_t timeout)
{
  builder_->timeout(timeout);
}
void
RPCServerBuilder::headerLimit(int32_t size)
{
  builder_->limit(size);
}
void
RPCServerBuilder::payloadLimit(int32_t size)
{
  payloadLimit_ = size;
}
void
RPCServerBuilder::listener(RPCServerEventListener listener, RPCOpaqueData userData, int32_t eventMask)
{
  listener_ = listener;
  listenerUserData_ = userData;
  listenerEventMask_ = eventMask;
}
void
RPCServerBuilder::keepalive(int64_t interval)
{
  builder_->keepalive(interval);
}
void
RPCServerBuilder::id(const char *id)
{
  builder_->id(id);
}
void
RPCServerBuilder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  if (logger) {
    logger_ = logger;
    level_ = level;
    loggerUserData_ = userData;
  } else {
    logger_ = defaultRPCLogger;
    level_ = defaultRPCLoggerLevel,
    loggerUserData_ = defaultRPCLoggerUserData;
  }
  builder_->logger(logger, level, userData);
}
void
RPCServerBuilder::dispatcher(RPCDispatcher *dispatcher)
{
  this->dispatcher_ = dispatcher;
  dispatcher->addRef();
  builder_->dispatcher(dispatcher);
}
void
RPCServerBuilder::flags(int32_t flags)
{
  builder_->flags(flags);
}
void
RPCServerBuilder::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  builder_->attachment(attachment, attachmentDestructor);
}
void
RPCServerBuilder::threadPoolSize(int32_t size)
{
  if (size < 0) {
    RPC_LOG_ERROR("Invalid thread pool size %d", size);
    return;
  }
  threadPoolSize_ = size;
}
void
RPCServerBuilder::connectionLimitPerIp(int32_t limit)
{
  if (limit < 1) {
    RPC_LOG_ERROR("Invalid limit %d", limit);
    return;
  }
  connectionLimitPerIp_ = limit;
}
void
RPCServerBuilder::connectRateLimitPerIp(int32_t limit)
{
  if (limit < 1) {
    RPC_LOG_ERROR("Invalid limit %d", limit);
    return;
  }
  connectRateLimitPerIp_ = limit;
}

RPCServer *
RPCServerBuilder::build()
{
  RPCServerImpl *server = new RPCServerImpl();
  RPCServer *wrapper = server->wrap();
  if (server->init(dispatcher_, builder_, listener_, listenerUserData_, listenerEventMask_,
        payloadLimit_, logger_, level_, loggerUserData_, threadPoolSize_,
        connectionLimitPerIp_, connectRateLimitPerIp_)) {
    server->shutdown();
    server->release();
    return NULL;
  } else {
    return wrapper;
  }
}

void
RPCServer::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor)
{
  impl_->attachment(attachment, destructor);
}

RPCServer::~RPCServer()
{
}

void
RPCServer::addRef()
{
  impl_->addRef();
}

bool
RPCServer::release()
{
  return impl_->release();
}

RPCOpaqueData
RPCServer::attachment() const
{
  return impl_->attachment();
}

void
RPCServer::shutdown()
{
  impl_->shutdown();
}

int32_t
RPCServer::submitAsync(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData)
{
  return impl_->submitAsync(channel, eventType, eventData, eventListener, eventListenerUserData);
}

int32_t
RPCServer::submitAsync(RPCChannel *channel, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData)
{
  return impl_->submitAsync(channel, eventListener, eventListenerUserData);
}

uint32_t
RPCServerImpl::AddressHash::operator()(const sockaddr_storage &lhs) const
{
  switch (lhs.ss_family) {
    case AF_INET:
      return reinterpret_cast<const sockaddr_in *>(&lhs)->sin_addr.s_addr;
    case AF_INET6:
      return safeRead<uint32_t>(reinterpret_cast<const sockaddr_in6 *>(&lhs)->sin6_addr.s6_addr + 12);
    default:
      return lhs.ss_family;
  }
}

bool
RPCServerImpl::AddressEquals::operator()(const sockaddr_storage &lhs, const sockaddr_storage &rhs) const
{
  if (lhs.ss_family != rhs.ss_family) {
    return false;
  }
  switch (lhs.ss_family) {
    case AF_INET:
      return memcmp(&reinterpret_cast<const sockaddr_in *>(&lhs)->sin_addr, &reinterpret_cast<const sockaddr_in *>(&rhs)->sin_addr, sizeof(reinterpret_cast<const sockaddr_in *>(&rhs)->sin_addr)) == 0;
    case AF_INET6:
      return memcmp(&reinterpret_cast<const sockaddr_in6 *>(&lhs)->sin6_addr, &reinterpret_cast<const sockaddr_in6 *>(&rhs)->sin6_addr, sizeof(reinterpret_cast<const sockaddr_in6 *>(&rhs)->sin6_addr)) == 0;
    default:
      return memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
  }
}

RPCServerImpl::RPCServerImpl()
  : wrapper_(NULL), channel_(NULL), channelDestroyed_(false), createEventDispatched_(false),
    initializedFlags_(0), dispatcher_(NULL), logger_(defaultRPCLogger), level_(defaultRPCLoggerLevel),
    loggerUserData_(defaultRPCLoggerUserData), threadPool_(NULL),
    stopping_(false)
{
}

RPCServerImpl::~RPCServerImpl()
{
  if (listenerEventMask_ & MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED) {
    listener_(wrapper_, NULL, MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED, NULL, listenerUserData_);
  }
  if (dispatcher_) {
    dispatcher_->release();
  }
  delete wrapper_;
}

void
RPCServerImpl::listener(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  static_cast<RPCServerImpl *>(userData)->onEvent(channel, eventType, eventData);
}

RPCServerImpl::AddressStatistics *
RPCServerImpl::checkAndGetStat(const sockaddr_storage &addr)
{
  AddressStatistics *stat = statistics_.get(addr);
  if (!stat) {
    AddressStatistics newStat;
    memset(&newStat, 0, sizeof(newStat));
    statistics_.put(addr, newStat);
    stat = statistics_.get(addr);
  }
  return stat;
}

bool
RPCServerImpl::connect(RPCChannel *channel)
{
  sockaddr_storage address;
  memset(&address, 0, sizeof(address));
  FriendHelper::getImpl<RPCChannelImpl>(channel)->remoteAddress(&address);
  AddressStatistics *stat = checkAndGetStat(address);
  StringLite addr;
  uint16_t port = 0;
  getInetAddressPresentation(&address, &addr, &port);
  ++stat->connections;
  time_t now = time(NULL) / 60;
  if (stat->minute != now) {
    stat->connects = 0;
    stat->busted = false;
    stat->minute = now;
  }
  if (stat->busted || stat->connections >= connectionLimitPerIp_) {
    RPC_LOG_INFO("New connection from %s:%u is rejected as number of connections from this address is "
        "%d or it has reached connect limit", addr.str(), port, stat->connections);
    return false;
  }
  if ((++stat->connects / 60) > connectRateLimitPerIp_) {
    stat->busted = true;
    RPC_LOG_INFO("New connection from %s:%u is rejected because it has reached connect limit", addr.str(), port);
    return false;
  }
  cleanupCandidates_.remove(address);
  RPC_LOG_INFO("New connection from %s:%u is accepted, number of connections from this address is %d",
      addr.str(), port, stat->connections);
  return true;
}

void
RPCServerImpl::disconnect(RPCChannel *channel)
{
  sockaddr_storage address;
  memset(&address, 0, sizeof(address));
  FriendHelper::getImpl<RPCChannelImpl>(channel)->remoteAddress(&address);
  AddressStatistics *stat = statistics_.get(address);
  if (stat) {
    StringLite addr;
    uint16_t port = 0;
    getInetAddressPresentation(&address, &addr, &port);
    --stat->connections;
    RPC_LOG_INFO("Connection from %s:%u is closed, number of connections from this address is %d",
        addr.str(), port, stat->connections);
    if (stat->connections <= 0) {
      cleanupCandidates_.put(address, 0);
    }
  }
}

void
RPCServerImpl::onEvent(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData)
{
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_DESTROYED:
      if (listenerEventMask_ & EVENT_TYPE_CHANNEL_DESTROYED) {
        listener_(wrapper_, channel, eventType, eventData, listenerUserData_);
      }
      if (channel_ == channel) {
        RPCLock lock(mutex_);
        channelDestroyed_ = true;
        cond_.broadcast();
      }
      break;
    case EVENT_TYPE_CHANNEL_CREATED:
      if (!createEventDispatched_) {
        listener_(wrapper_, channel, MOCA_RPC_EVENT_TYPE_SERVER_CREATED, eventData, listenerUserData_);
        createEventDispatched_ = true;
      }
      listener_(wrapper_, channel, eventType, eventData, listenerUserData_);
      break;
    case EVENT_TYPE_CHANNEL_CONNECTED:
      if (!connect(channel)) {
        channel->close();
      } else {
        listener_(wrapper_, channel, eventType, eventData, listenerUserData_);
      }
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      disconnect(channel);
      listener_(wrapper_, channel, eventType, eventData, listenerUserData_);
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
    case EVENT_TYPE_CHANNEL_RESPONSE:
      {
        PacketEventData *packet = static_cast<PacketEventData *>(eventData);
        if (packet->payloadSize > payloadLimit_) {
          RPC_LOG_WARN("Payload size %d is greater than limit of %d, close connection", packet->payloadSize, payloadLimit_);
          channel->close();
          return;
        }
      }
      /* FALL THROUGH */
    default:
      listener_(wrapper_, channel, eventType, eventData, listenerUserData_);
  }
}

int32_t
RPCServerImpl::init(RPCDispatcher *dispatcher, RPCChannel::Builder *builder, RPCServerEventListener listener,
                    RPCOpaqueData listenerUserData, int32_t listenerEventMask, int32_t payloadLimit,
                    RPCLogger logger, RPCLogLevel loggerLevel, RPCOpaqueData loggerUserData,
                    int32_t threadPoolSize, int32_t connectionLimitPerIp, int32_t connectRateLimitPerIp)
{
  int32_t st;
  listener_ = listener;
  listenerUserData_ = listenerUserData;
  listenerEventMask_ = listenerEventMask;
  connectionLimitPerIp_ = connectionLimitPerIp;
  connectRateLimitPerIp_ = connectRateLimitPerIp;
  if (threadPoolSize > 0) {
    threadPool_ = new RPCThreadPool(logger, loggerLevel, loggerUserData);
    MOCA_RPC_DO(threadPool_->init(threadPoolSize));
  }
  dispatcher_ = dispatcher;
  dispatcher->addRef();
  payloadLimit_ = payloadLimit;
  logger_ = logger;
  level_ = loggerLevel;
  loggerUserData_ = loggerUserData;
  if ((st = uv_timer_init(FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher)->loop(), &timer_))) {
    CONVERT_UV_ERROR(st, st, logger, loggerLevel, loggerUserData);
    goto cleanupExit;
  }
  timer_.data = this;
  if ((st = uv_timer_start(&timer_, onTimer, 15000, 15000))) {
    CONVERT_UV_ERROR(st, st, logger, loggerLevel, loggerUserData);
    goto cleanupExit;
  }
  initializedFlags_ |= TIMER_INITIALIZED;
  builder->listener(RPCServerImpl::listener, this, listenerEventMask | EVENT_TYPE_CHANNEL_DESTROYED);
  channel_ = builder->build();
  if (channel_ == NULL) {
    st = RPC_INTERNAL_ERROR;
  }

cleanupExit:
  return st;
}

void
RPCServerImpl::onTimer(uv_timer_t *handle)
{
  static_cast<RPCServerImpl *>(handle->data)->onTimer();
}

void
RPCServerImpl::onTimer()
{
  cleanupCandidates_.visitAll(visitCleanupCandidate, this);
  cleanupCandidates_.clear();
}

void
RPCServerImpl::visitCleanupCandidate(const sockaddr_storage &key, const int32_t &value, RPCOpaqueData userData)
{
  static_cast<RPCServerImpl *>(userData)->visitCleanupCandidate(key);
}

void
RPCServerImpl::visitCleanupCandidate(const sockaddr_storage &addr)
{
  statistics_.remove(addr);
}

void
RPCServerImpl::attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor)
{
  channel_->attachment(attachment, destructor);
}

RPCOpaqueData
RPCServerImpl::attachment() const
{
  return channel_->attachment();
}

void
RPCServerImpl::onAsyncShutdown(RPCOpaqueData userData)
{
  static_cast<RPCServerImpl *>(userData)->onAsyncShutdown();
}

void
RPCServerImpl::onAsyncShutdownDone(uv_handle_t *handle)
{
  static_cast<RPCServerImpl *>(handle->data)->release();
}

void
RPCServerImpl::onAsyncShutdown()
{
  uv_timer_stop(&timer_);
  uv_close(reinterpret_cast<uv_handle_t *>(&timer_), onAsyncShutdownDone);
}

void
RPCServerImpl::shutdown()
{
  bool expecting = false;
  if (!stopping_.compareAndSet(expecting, true)) {
    return;
  }
  if ((initializedFlags_ & TIMER_INITIALIZED) != 0) {
    addRef();
    if (MOCA_RPC_FAILED(FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher_)->submitAsync(onAsyncShutdown, this))) {
      release();
    }
  }
  if (threadPool_) {
    RPCThreadPool *threadPool = threadPool_;
    threadPool_ = NULL;
    RPC_MEMORY_BARRIER_FULL();
    threadPool->shutdown();
    delete threadPool;
  }
  if (channel_) {
    channel_->close();
    channel_->release();
    RPCLock lock(mutex_);
    while (!channelDestroyed_) {
      cond_.wait(mutex_);
    }
    channel_ = NULL;
  }
}

RPCServerImpl::AsyncTaskContext *
RPCServerImpl::duplicatePacketEventData(RPCOpaqueData eventData)
{
  PacketEventData *packet = static_cast<PacketEventData *>(eventData);
  AsyncTaskContext *newContext = static_cast<AsyncTaskContext *>(malloc(sizeof(AsyncTaskContext) + sizeof(PacketEventData) + (packet->headers ? sizeof(*packet->headers) : 0)));
  PacketEventData *newPacket = unsafeGet<PacketEventData>(newContext, sizeof(AsyncTaskContext));
  *newPacket = *packet;
  if (packet->headers) {
    KeyValuePairs<StringLite, StringLite> *newHeaders = new (unsafeGet<KeyValuePairs<StringLite, StringLite> >(newPacket, sizeof(PacketEventData))) KeyValuePairs<StringLite, StringLite>;
    *newHeaders = *packet->headers;
    newPacket->headers = newHeaders;
  }
  newContext->eventData = newPacket;
  return newContext;
}

RPCServerImpl::AsyncTaskContext *
RPCServerImpl::duplicatePayloadEventData(RPCOpaqueData eventData)
{
  PayloadEventData *payload = static_cast<PayloadEventData *>(eventData);
  AsyncTaskContext *newContext = static_cast<AsyncTaskContext *>(malloc(sizeof(AsyncTaskContext) + sizeof(PayloadEventData) + payload->size));
  PayloadEventData *newPayload = unsafeGet<PayloadEventData>(newContext, sizeof(AsyncTaskContext));
  *newPayload = *payload;
  if (payload->size > 0) {
    char *payloadData = unsafeGet<char>(newPayload, sizeof(PayloadEventData));
    memcpy(payloadData, payload->payload, payload->size);
    newPayload->payload = payloadData;
  }
  newContext->eventData = newPayload;
  return newContext;
}

RPCServerImpl::AsyncTaskContext *
RPCServerImpl::duplicateErrorEventData(RPCOpaqueData eventData)
{
  ErrorEventData *error = static_cast<ErrorEventData *>(eventData);
  AsyncTaskContext *newContext = static_cast<AsyncTaskContext *>(malloc(sizeof(AsyncTaskContext) + sizeof(ErrorEventData)));
  ErrorEventData *newError = unsafeGet<ErrorEventData>(newContext, sizeof(AsyncTaskContext));
  *newError = *error;
  newContext->eventData = newError;
  return newContext;
}

void
RPCServerImpl::onAsyncTask(AsyncTaskContext *context)
{
  context->eventListener(wrapper_, context->channel, context->eventType, context->eventData, context->eventListenerUserData);
  if (context->channel) {
    context->channel->release();
  }
  if (context->eventType == EVENT_TYPE_CHANNEL_REQUEST || context->eventType == EVENT_TYPE_CHANNEL_RESPONSE) {
    PacketEventData *packet = static_cast<PacketEventData *>(context->eventData);
    if (packet->headers) {
      packet->headers->~KeyValuePairs<StringLite, StringLite>();
    }
  }
  free(context);
  release();
}

void
RPCServerImpl::asyncTaskEntry(RPCOpaqueData userData)
{
  static_cast<AsyncTaskContext *>(userData)->server->onAsyncTask(static_cast<AsyncTaskContext *>(userData));
}

int32_t
RPCServerImpl::submitAsync(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData)
{
  if (threadPool_ == NULL) {
    RPC_LOG_WARN("Thread pool is not configured, dispatch event in caller's thread");
    eventListener(wrapper_, channel, eventType, eventData, eventListenerUserData);
    return RPC_OK;
  }

  AsyncTaskContext *context;
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_REQUEST:
    case EVENT_TYPE_CHANNEL_RESPONSE:
      context = duplicatePacketEventData(eventData);
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      context = duplicatePayloadEventData(eventData);
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      context = duplicateErrorEventData(eventData);
      break;
    case EVENT_TYPE_CHANNEL_DESTROYED:
      RPC_LOG_ERROR("Channel destroyed event may not be asynchronously submitted");
      return MOCA_RPC_INVALID_ARGUMENT;
    case EVENT_TYPE_SERVER_DESTROYED:
      RPC_LOG_ERROR("Server destroyed event may not be asynchronously submitted");
      return MOCA_RPC_INVALID_ARGUMENT;
    default:
      context = static_cast<AsyncTaskContext *>(malloc(sizeof(AsyncTaskContext)));
      context->eventData = NULL;
      break;
  }
  if (channel != NULL) {
    channel->addRef();
  }
  context->server = this;
  context->channel = channel;
  context->eventType = eventType;
  context->eventListener = eventListener;
  context->eventListenerUserData = eventListenerUserData;
  this->addRef();
  return threadPool_->submit(asyncTaskEntry, context, channel != NULL ? FriendHelper::getImpl<RPCChannelImpl>(channel)->key() : 0);
}

int32_t
RPCServerImpl::submitAsync(RPCChannel *channel, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData)
{
  if (threadPool_ == NULL) {
    RPC_LOG_WARN("Thread pool is not configured, dispatch event in caller's thread");
    eventListener(wrapper_, channel, 0, NULL, eventListenerUserData);
    return RPC_OK;
  }

  AsyncTaskContext *context = static_cast<AsyncTaskContext *>(malloc(sizeof(AsyncTaskContext)));
  if (channel != NULL) {
    channel->addRef();
  }
  context->server = this;
  context->channel = channel;
  context->eventType = 0;
  context->eventListener = eventListener;
  context->eventListenerUserData = eventListenerUserData;
  context->eventData = NULL;
  this->addRef();
  return threadPool_->submit(asyncTaskEntry, context, channel != NULL ? FriendHelper::getImpl<RPCChannelImpl>(channel)->key() : 0);
}

END_MOCA_RPC_NAMESPACE
