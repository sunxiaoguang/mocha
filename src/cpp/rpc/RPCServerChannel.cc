#include "RPCServerChannel.h"
#include "RPCClientChannel.h"
#include "RPCDispatcher.h"
#include <assert.h>

BEGIN_MOCHA_RPC_NAMESPACE

struct TimerEventArgument
{
  RPCServerChannel *channel;
  uint64_t now;
  uint64_t keepaliveDeadline;
  uint64_t timeoutDeadline;
};

RPCServerChannel::RPCServerChannel()
  : currentChannelId_(0), activeChannels_(0), readHead_(NULL), readTail_(NULL),
    writeHead_(NULL), writeTail_(NULL), lastTimeoutTime_(0), lastKeepaliveTime_(0)
{
  remoteAddress_.assign("0.0.0.0");
  remotePort_ = 0;
  localAddress_.assign("0.0.0.0");
  localPort_ = 0;
}

RPCServerChannel::~RPCServerChannel()
{
}

void
RPCServerChannel::onNewConnection()
{
  RPCClientChannel *channel = new RPCClientChannel();
  channel->wrap();

  int32_t st = channel->init(this);
  if (st) {
    RPC_LOG_ERROR("Could not initialize client channel. %d:%s", st, errorString(st));
    channel->release();
  }
}

RPCChannelId
RPCServerChannel::addClientChannel(RPCClientChannel *channel)
{
  RPCChannelId channelId = currentChannelId_++;
  ++activeChannels_;
  RPC_LOG_DEBUG("Number of active connections is %u", activeChannels_);
  linkTo(&readHead_, &readTail_, channel, OFFSET_OF(RPCClientChannel, readPrev), OFFSET_OF(RPCClientChannel, readNext));
  linkTo(&writeHead_, &writeTail_, channel, OFFSET_OF(RPCClientChannel, writePrev), OFFSET_OF(RPCClientChannel, writeNext));
  return channelId;
}

void
RPCServerChannel::onNewConnection(uv_stream_t *server, int st)
{
  RPCServerChannel *channel = static_cast<RPCServerChannel *>(server->data);
  if (st < 0) {
    LOGGER_ERROR(channel->logger_, channel->level_, channel->loggerUserData_, "Could not accept new connection");
    CONVERT_UV_ERROR(channel->asyncStatus_, st, channel->logger_, channel->level_, channel->loggerUserData_);
    channel->processErrorHook(st);
    return;
  }

  channel->onNewConnection();
}

int32_t
RPCServerChannel::onResolved(struct addrinfo *result)
{
  assert(dispatcher_->isDispatchingThread());
  lastKeepaliveTime_ = lastTimeoutTime_ = uv_now(loop());
  int32_t st = RPC_OK;
  struct addrinfo *address = NULL;
  uv_tcp_simultaneous_accepts(&handle_, 1);
  uv_os_fd_t fd;
#ifdef SO_REUSEPORT
  if (uv_fileno(reinterpret_cast<uv_handle_t *>(&handle_), &fd) == 0) {
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
  }
#endif
  for (address = result; address; address = address->ai_next) {
    if ((st = uv_tcp_bind(&handle_, address->ai_addr, 0))) {
      RPC_LOG_ERROR("Could not bind. %s", uv_err_name(st));
      CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
      continue;
    }
    if ((st = uv_listen(reinterpret_cast<uv_stream_t *>(&handle_), 128, onNewConnection))) {
      RPC_LOG_ERROR("Could not listen. %s", uv_err_name(st));
      CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
      continue;
    }
    getAddress(uv_tcp_getsockname, &localAddress_, &localPort_);
    break;
  }

  if (isBlocking()) {
    finishAsync(st);
  }
  if (st == RPC_OK) {
    fireListeningEvent();
  } else {
    processErrorHook(st);
  }
  return st;
}

void
RPCServerChannel::visitChannel(RPCClientChannel *channel)
{
  RPC_LOG_DEBUG("Closing client channel %lld@%p", static_cast<long long>(channel->channelId()), channel);
  channel->close();
}

bool
RPCServerChannel::visitChannel(RPCClientChannel *channel, RPCOpaqueData userData)
{
  static_cast<RPCServerChannel *>(userData)->visitChannel(channel);
  return true;
}

void
RPCServerChannel::onAsyncClose()
{
  visitChannel(&readHead_, &readTail_, OFFSET_OF(RPCClientChannel, readPrev),
      OFFSET_OF(RPCClientChannel, readNext), 0x7FFFFFFF, visitChannel, this);
  RPCChannelImpl::onAsyncClose();
}

void
RPCServerChannel::removeClientChannel(RPCChannelId channelId, RPCClientChannel *channel)
{
  unlinkFrom(&readHead_, &readTail_, channel, OFFSET_OF(RPCClientChannel, readPrev), OFFSET_OF(RPCClientChannel, readNext));
  unlinkFrom(&writeHead_, &writeTail_, channel, OFFSET_OF(RPCClientChannel, writePrev), OFFSET_OF(RPCClientChannel, writeNext));
  channel->release();
  --activeChannels_;
  RPC_LOG_DEBUG("Number of active connections is %u", activeChannels_);
}

void
RPCServerChannel::updateReadTime(RPCChannelId channelId, RPCClientChannel *channel)
{
  unlinkFrom(&readHead_, &readTail_, channel, OFFSET_OF(RPCClientChannel, readPrev), OFFSET_OF(RPCClientChannel, readNext));
  linkTo(&readHead_, &readTail_, channel, OFFSET_OF(RPCClientChannel, readPrev), OFFSET_OF(RPCClientChannel, readNext));
}

void
RPCServerChannel::updateWriteTime(RPCChannelId channelId, RPCClientChannel *channel)
{
  unlinkFrom(&writeHead_, &writeTail_, channel, OFFSET_OF(RPCClientChannel, writePrev), OFFSET_OF(RPCClientChannel, writeNext));
  linkTo(&writeHead_, &writeTail_, channel, OFFSET_OF(RPCClientChannel, writePrev), OFFSET_OF(RPCClientChannel, writeNext));
}

void
RPCServerChannel::onTimer(uint64_t now)
{
  RPC_LOG_DEBUG("Timer is fired to check idle connections");
  TimerEventArgument argument = {this, now,
    now - static_cast<uint64_t>(keepaliveInterval_),
    now - static_cast<uint64_t>(timeout_)};
  if (isKeepaliveEnabled() && now > static_cast<uint64_t>(keepaliveInterval_) + lastKeepaliveTime_) {
    visitChannel(&writeHead_, &writeTail_, OFFSET_OF(RPCClientChannel, writePrev),
        OFFSET_OF(RPCClientChannel, writeNext), 0x7FFFFFFF, sendHeartbeat, &argument);
    lastKeepaliveTime_ = now;
  }
  if (isTimeoutEnabled() && now > static_cast<uint64_t>(timeout_) + lastTimeoutTime_) {
    visitChannel(&readHead_, &readTail_, OFFSET_OF(RPCClientChannel, readPrev),
        OFFSET_OF(RPCClientChannel, readNext), 0x7FFFFFFF, checkTimeout, &argument);
    lastTimeoutTime_ = now;
  }
}

void
RPCServerChannel::linkTo(RPCClientChannel **head, RPCClientChannel **tail, RPCClientChannel *channel, size_t prev, size_t next)
{
  *unsafeGet<RPCClientChannel *>(channel, prev) = NULL;
  *unsafeGet<RPCClientChannel *>(channel, next) = *head;
  if (*head) {
    *unsafeGet<RPCClientChannel *>(*head, prev) = channel;
  }
  *head = channel;
  if (*tail == NULL) {
    *tail = channel;
  }
}

void
RPCServerChannel::unlinkFrom(RPCClientChannel **head, RPCClientChannel **tail, RPCClientChannel *channel, size_t prev, size_t next)
{
  RPCClientChannel *realNext = *unsafeGet<RPCClientChannel *>(channel, next);
  RPCClientChannel *realPrev = *unsafeGet<RPCClientChannel *>(channel, prev);
  if (*head == channel) {
    *head = realNext;
  }
  if (*tail == channel) {
    *tail = realPrev;
  }

  if (realNext) {
    *unsafeGet<RPCClientChannel *>(realNext, prev) = realPrev;
  }
  if (realPrev) {
    *unsafeGet<RPCClientChannel *>(realPrev, next) = realNext;
  }
}

void
RPCServerChannel::visitChannel(RPCClientChannel **head, RPCClientChannel **tail, size_t prev, size_t next, int32_t limit, RPCServerChannel::ChannelVisitor visitor, RPCOpaqueData userData)
{
  RPCClientChannel *channel = NULL;
  RPCClientChannel *lruPrev = NULL;
  for (channel = *tail; limit > 0 && channel != NULL; channel = lruPrev) {
    lruPrev = *unsafeGet<RPCClientChannel *>(channel, prev);
    if (!visitor(channel, userData)) {
      break;
    }
    --limit;
  }
}

bool
RPCServerChannel::sendHeartbeat(RPCClientChannel *channel, RPCOpaqueData userData)
{
  TimerEventArgument *argument = static_cast<TimerEventArgument *>(userData);
  if (channel->lastWriteTime() <= argument->keepaliveDeadline) {
    if (channel->isEstablished()) {
      LOGGER_DEBUG(argument->channel->logger_, argument->channel->level_, argument->channel->loggerUserData_,
          "Channel %llu has not sent any data for %llu ms, send keepalive message to it",
          static_cast<unsigned long long>(channel->channelId()),
          argument->now - static_cast<unsigned long long>(channel->lastWriteTime()));
      channel->keepalive();
    }
    return true;
  } else {
    return false;
  }
}

bool
RPCServerChannel::checkTimeout(RPCClientChannel *channel, RPCOpaqueData userData)
{
  TimerEventArgument *argument = static_cast<TimerEventArgument *>(userData);
  if (channel->lastReadTime() <= argument->timeoutDeadline) {
    LOGGER_ERROR(argument->channel->logger_, argument->channel->level_, argument->channel->loggerUserData_,
        "Channel %llu has not received any data for %llu ms, close it",
        static_cast<unsigned long long>(channel->channelId()),
        argument->now - static_cast<unsigned long long>(channel->lastReadTime()));
    channel->close();
    return true;
  } else {
    return false;
  }
}

END_MOCHA_RPC_NAMESPACE
