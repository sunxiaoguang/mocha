#include "RPCClientChannel.h"
#include "RPCServerChannel.h"
#include "RPCDispatcher.h"
#include <zlib.h>
#include <assert.h>

BEGIN_MOCA_RPC_NAMESPACE

struct WriteRequest
{
  uv_write_t request;
  ChainedBuffer *head;
  RPCClientChannel *channel;
};

RPCClientChannel::RPCClientChannel()
  : protocol_(this, closeChannel, protocolEventListener, this, protocolWriteSink, this, sizeof(WriteRequest), 1024),
    channelId_(RPC_INVALID_CHANNEL_ID), owner_(NULL), buffer_(NULL), lastWriteTime_(0), lastReadTime_(0),
    readPrev(NULL), readNext(NULL), writePrev(NULL), writeNext(NULL)
{
}

void
RPCClientChannel::updateReadTime()
{
  uint64_t now = uv_now(loop());
  if (now - lastReadTime_ > 1000) {
    lastReadTime_ = now;
    if (owner_) {
      owner_->updateReadTime(channelId_, this);
    }
  }
}

void
RPCClientChannel::updateWriteTime()
{
  uint64_t now = uv_now(loop());
  if (now - lastWriteTime_ > 1000) {
    lastWriteTime_ = now;
    if (owner_) {
      owner_->updateWriteTime(channelId_, this);
    }
  }
}

RPCClientChannel::~RPCClientChannel()
{
  if (owner_) {
    owner_->release();
  }
}

void
RPCClientChannel::onEof()
{
  close();
}

void
RPCClientChannel::onError()
{
  close();
}

void
RPCClientChannel::onRead(const char *data, size_t size)
{
  buffer_->allocate(size);
  protocol_.onRead(size);
  updateReadTime();
}

void
RPCClientChannel::onAllocate(uv_handle_t *handle, size_t size, uv_buf_t *buf)
{
  RPCClientChannel *channel = static_cast<RPCClientChannel *>(handle->data);
  channel->buffer_ = channel->protocol_.checkAndGetBuffer(&size);
  *buf = uv_buf_init(static_cast<char *>(channel->buffer_->get<char>(channel->buffer_->getSize())), size);
}

void
RPCClientChannel::onRead(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
  RPCClientChannel *channel = static_cast<RPCClientChannel *>(stream->data);
  if (nread < 0) {
    if (nread == UV_EOF) {
      channel->onEof();
    } else {
      channel->onError();
    }
  } else if (nread > 0) {
    channel->onRead(buf->base, nread);
  }
}

int32_t
RPCClientChannel::init(RPCServerChannel *server)
{
  server->inherit(this);
  assert(dispatcher_->isDispatchingThread());
  int32_t st;
  if ((st = asyncToken_.init())) {
    processErrorHook(st);
    return st;
  }
  uv_tcp_init(eventLoop_, &handle_);
  uv_tcp_nodelay(&handle_, 1);
  handle_.data = this;
  if ((st = uv_accept(reinterpret_cast<uv_stream_t *>(server->handle()), reinterpret_cast<uv_stream_t *>(&handle_))) ||
      (st = protocol_.init(localId_, logger_, level_, loggerUserData_, 0, limit_, channelFlags_))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
  } else {
    initializedState();
    owner_ = server;
    owner_->addRef();
    key_ = channelId_ = server->addClientChannel(this);
    getAddress(uv_tcp_getsockname, &localAddress_, &localPort_);
    getAddress(uv_tcp_getpeername, &remoteAddress_, &remotePort_);
    int32_t size = sizeof(remoteSockAddress_);
    getAddress(uv_tcp_getpeername, &remoteSockAddress_, &size);
    RPC_LOG_INFO("Connection from %s:%u to %s:%u is established",
        remoteAddress_.str(), static_cast<uint32_t>(remotePort_),
        localAddress_.str(), static_cast<uint32_t>(localPort_));
    fireCreatedEvent();
    fireConnectedEvent();
    updateReadTime();
    uv_read_start(reinterpret_cast<uv_stream_t *>(&handle_), onAllocate, onRead);
    st = protocol_.sendNegotiation();
  }
  return st;
}

void
RPCClientChannel::onWrite(uv_write_t *req, int32_t st)
{
  WriteRequest *packet = static_cast<WriteRequest *>(req->data);
  if (packet) {
    ChainedBuffer *head = packet->head;
    if (st) {
      CONVERT_UV_ERROR(st, st, packet->channel->logger_, packet->channel->level_, packet->channel->loggerUserData_);
      packet->channel->processErrorHook(st);
    }
    packet->channel->updateWriteTime();
    packet->channel->release();
    ChainedBuffer::destroy(&head);
  }
}

void
RPCClientChannel::doWrite(ChainedBuffer **buffer)
{
  bool dispatchingThread = isDispatchingThread();

  ChainedBuffer *head = *buffer;
  WriteRequest *packetRequest = head->get<WriteRequest>();
  packetRequest->head = head;
  packetRequest->channel = this;
  *buffer = NULL;

  addRef();
  int32_t st;
  if (dispatchingThread) {
    st = doWrite(packetRequest, sizeof(WriteRequest));
  } else {
    st = doWriteAsync(packetRequest);
  }

  if (MOCA_RPC_FAILED(st)) {
    RPC_LOG_ERROR("Caught error when writing data. %d", st);
  }
}

int32_t
RPCClientChannel::doWrite(WriteRequest *packetRequest, size_t packetRequestSize)
{
  size_t iovCapacity = 16;
  uv_buf_t iovBuff[16];
  uv_buf_t *iov = iovBuff;
  size_t iovsize = 0;
  uv_write_t *request = &packetRequest->request;
  request->data = packetRequest;
  int32_t st;
  ChainedBuffer *buffer = packetRequest->head;

  iov[iovsize].base = buffer->get<char>(packetRequestSize);
  iov[iovsize].len = buffer->getSize() - packetRequestSize;
  ++iovsize;
  buffer = buffer->next;

  while (buffer) {
    iov[iovsize].base = buffer->get<char>();
    iov[iovsize].len = buffer->getSize();
    if (++iovsize == iovCapacity) {
      iovCapacity = iovsize + 8;
      size_t newSize = sizeof(uv_buf_t) * iovCapacity;
      if (iov == iovBuff) {
        iov = static_cast<uv_buf_t *>(malloc(newSize));
        memcpy(iov, iovBuff, sizeof(iovBuff));
      } else {
        iov = static_cast<uv_buf_t *>(realloc(iov, newSize));
      }
    }
    buffer = buffer->next;
  }

  assert(dispatcher_->isDispatchingThread());
  if ((st = uv_write(request, const_cast<uv_stream_t *>(reinterpret_cast<const uv_stream_t *>(&handle_)), iov, iovsize, onWrite))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
    buffer = packetRequest->head;
    ChainedBuffer::destroy(&buffer);
    release();
  }

  if (iov != iovBuff) {
    free(iov);
  }
  return st;
}

void
RPCClientChannel::onAsyncWrite(RPCOpaqueData data)
{
  WriteRequest *request = static_cast<WriteRequest *>(data);
  request->channel->doWrite(request, sizeof(WriteRequest));
}

int32_t
RPCClientChannel::doWriteAsync(WriteRequest *packetRequest)
{
  ChainedBuffer *head = packetRequest->head;
  packetRequest->request.data = packetRequest;
  packetRequest->channel = this;
  int32_t st;
  if ((st = FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher_)->submitAsync(onAsyncWrite, packetRequest, key()))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    processErrorHook(st);
    ChainedBuffer::destroy(&head);
    release();
  }
  return st;
}

void
RPCClientChannel::onConnect()
{
  assert(dispatcher_->isDispatchingThread());
  updateTime();
  getAddress(uv_tcp_getsockname, &localAddress_, &localPort_);
  getAddress(uv_tcp_getpeername, &remoteAddress_, &remotePort_);
  RPC_LOG_INFO("Connection from %s:%u to %s:%u is established",
      localAddress_.str(), static_cast<uint32_t>(localPort_),
      remoteAddress_.str(), static_cast<uint32_t>(remotePort_));
  fireConnectedEvent();
  if (isBlocking()) {
    finishAsync(MOCA_RPC_OK);
  }
  uv_read_start(reinterpret_cast<uv_stream_t *>(&handle_), onAllocate, onRead);
  protocol_.sendNegotiation();
}

void
RPCClientChannel::onConnect(uv_connect_t* req, int st)
{
  RPCClientChannel *channel = reinterpret_cast<RPCClientChannel *>(req->data);
  if (st) {
    CONVERT_UV_ERROR(st, st, channel->logger_, channel->level_, channel->loggerUserData_);
    channel->processErrorHook(st);
  } else {
    channel->onConnect();
  }
  channel->release();
  free(req);
}

int32_t
RPCClientChannel::onResolved(struct addrinfo *result)
{
  int32_t st = RPC_OK;
  struct addrinfo *address = NULL;
  assert(dispatcher_->isDispatchingThread());
  for (address = result; address; address = address->ai_next) {
    uv_connect_t *connect = static_cast<uv_connect_t *>(malloc(sizeof(uv_connect_t)));
    connect->data = this;
    if ((st = uv_tcp_connect(connect, handle(), address->ai_addr, onConnect))) {
      RPC_LOG_ERROR("Could not connect. %s", uv_err_name(st));
      CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
      continue;
    }
    addRef();
    break;
  }

  if (MOCA_RPC_FAILED(st) || MOCA_RPC_FAILED(st = protocol_.init(localId_, logger_, level_, loggerUserData_, 0, limit_, channelFlags_))) {
    if (isBlocking()) {
      finishAsync(st);
    }
    processErrorHook(st);
  }
  return st;
}

void
RPCClientChannel::onClose()
{
  if (owner_) {
    owner_->removeClientChannel(channelId_, this);
  }
  RPCChannelImpl::onClose();
}

void
RPCClientChannel::protocolEventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  static_cast<RPCClientChannel *>(userData)->doFireEvent(eventType, eventData);
}

void
RPCClientChannel::protocolWriteSink(ChainedBuffer **buffer, RPCOpaqueData argument)
{
  static_cast<RPCClientChannel *>(argument)->doWrite(buffer);
}

void
RPCClientChannel::onTimer(uint64_t now)
{
  if (isKeepaliveEnabled() && (now - lastWriteTime()) > static_cast<uint64_t>(keepaliveInterval_)) {
    RPC_LOG_DEBUG("Channel %p has not sent any data for %llu ms, send keepalive message to it",
        this, now - static_cast<unsigned long long>(lastWriteTime()));
    if (keepalive() == RPC_CANCELED) {
      updateWriteTime();
    }
  }
  if (isTimeoutEnabled() && (now - lastReadTime()) > static_cast<uint64_t>(timeout_)) {
    RPC_LOG_DEBUG("Channel %p has not received any data for %llu ms, close it",
        this, now - static_cast<unsigned long long>(lastReadTime()));
    close();
  }
}

END_MOCA_RPC_NAMESPACE
