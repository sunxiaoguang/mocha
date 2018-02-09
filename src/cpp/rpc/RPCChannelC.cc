#include "RPCChannelC.h"

struct MochaRPCChannelBuilder
{
  RPCChannel::Builder *impl;
  MochaRPCChannel *channel;
};

static void MochaRPCEventListenerAdapter(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MochaRPCChannel *wrapper = static_cast<MochaRPCChannel *>(userData);
  MochaRPCChannel *attachment = static_cast<MochaRPCChannel *>(channel->attachment());
  switch (eventType) {
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED:
      if (attachment == NULL) {
        channel->attachment((attachment = static_cast<MochaRPCChannel *>(calloc(1, sizeof(MochaRPCChannel)))), NULL);
      }
      wrapper->refcount++;
      attachment->impl = channel;
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED) {
        wrapper->listener(attachment, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED:
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
        wrapper->listener(attachment, eventType, eventData, wrapper->userData);
      }
      if (attachment != wrapper) {
        destroy(attachment);
      }
      if (--wrapper->refcount == 0) {
        destroy(wrapper);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
      /* FALL THROUGH */
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
      {
        PacketEventData *packet = static_cast<PacketEventData *>(eventData);
        convert(packet->headers, wrapper);
        packet->headers = reinterpret_cast<KeyValuePairs<StringLite, StringLite> *>(wrapper->headers);
      }
      /* FALL THROUGH */
    default:
      wrapper->listener(attachment, eventType, eventData, wrapper->userData);
      break;
  }
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderCreate(void)
{
  MochaRPCChannel *channel = static_cast<MochaRPCChannel *>(calloc(1, sizeof(MochaRPCChannel)));
  MochaRPCChannelBuilder *builder = static_cast<MochaRPCChannelBuilder *>(calloc(1, sizeof(MochaRPCChannelBuilder)));
  if (channel == NULL || builder == NULL) {
    goto cleanupExit;
  }
  builder->impl = RPCChannel::newBuilder();
  if (builder->impl == NULL) {
    goto cleanupExit;
  }
  builder->channel = channel;
  builder->impl->attachment(channel, NULL);
  return builder;
cleanupExit:
  destroy(channel);
  free(builder);
  return NULL;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderBind(MochaRPCChannelBuilder *builder, const char *address)
{
  builder->impl->bind(address);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderConnect(MochaRPCChannelBuilder *builder, const char *address)
{
  builder->impl->connect(address);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderTimeout(MochaRPCChannelBuilder *builder, int64_t timeout)
{
  builder->impl->timeout(timeout);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderLimit(MochaRPCChannelBuilder *builder, int32_t size)
{
  builder->impl->limit(size);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderListener(MochaRPCChannelBuilder *builder, MochaRPCEventListener listener, MochaRPCOpaqueData userData, int32_t eventMask)
{
  builder->channel->listener = listener;
  builder->channel->userData = userData;
  builder->channel->eventMask = eventMask;
  builder->impl->listener(MochaRPCEventListenerAdapter, builder->channel, eventMask | MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED | MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderKeepalive(MochaRPCChannelBuilder *builder, int64_t interval)
{
  builder->impl->keepalive(interval);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderId(MochaRPCChannelBuilder *builder, const char *id)
{
  builder->impl->id(id);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderLogger(MochaRPCChannelBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData userData)
{
  builder->impl->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), userData);
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderDispatcher(MochaRPCChannelBuilder *builder, MochaRPCDispatcher *dispatcher)
{
  builder->impl->dispatcher(reinterpret_cast<RPCDispatcher *>(dispatcher));
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderAttachment(MochaRPCChannelBuilder *builder, MochaRPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  builder->channel->attachment = attachment;
  builder->channel->attachmentDestructor = attachmentDestructor;
  return builder;
}

MochaRPCChannelBuilder *
MochaRPCChannelBuilderFlags(MochaRPCChannelBuilder *builder, int32_t flags)
{
  builder->impl->flags(flags);
  return builder;
}

int32_t
MochaRPCChannelBuilderBuild(MochaRPCChannelBuilder *builder, MochaRPCChannel **channel)
{
  builder->channel->impl = builder->impl->build();

  int32_t st;
  if (builder->channel->impl == NULL) {
    st = MOCHA_RPC_INTERNAL_ERROR;
  } else {
    st = MOCHA_RPC_OK;
  }
  *channel = builder->channel;
  builder->channel = static_cast<MochaRPCChannel *>(calloc(1, sizeof(MochaRPCChannel)));
  return st;
}

void
MochaRPCChannelBuilderDestroy(MochaRPCChannelBuilder *builder)
{
  delete builder->impl;
  destroy(builder->channel);
  free(builder);
}

void
MochaRPCChannelAddRef(MochaRPCChannel *channel)
{
  channel->impl->addRef();
}

int32_t
MochaRPCChannelRelease(MochaRPCChannel *channel)
{
  return channel->impl->release();
}

void
MochaRPCChannelClose(MochaRPCChannel *channel)
{
  channel->impl->close();
}

int32_t
MochaRPCChannelLocalAddress(MochaRPCChannel *channel, char **localAddress, uint16_t *port)
{
  StringLite tmp;
  int32_t st = channel->impl->localAddress(&tmp, port);
  if (st == MOCHA_RPC_OK) {
    size_t size;
    tmp.release(localAddress, &size);
  }
  return st;
}

int32_t
MochaRPCChannelRemoteAddress(MochaRPCChannel *channel, char **remoteAddress, uint16_t *port)
{
  StringLite tmp;
  int32_t st = channel->impl->remoteAddress(&tmp, port);
  if (st == MOCHA_RPC_OK) {
    size_t size;
    tmp.release(remoteAddress, &size);
  }
  return st;
}

int32_t
MochaRPCChannelLocalId(MochaRPCChannel *channel, char **localId)
{
  StringLite tmp;
  int32_t st = channel->impl->localId(&tmp);
  if (st == MOCHA_RPC_OK) {
    size_t size;
    tmp.release(localId, &size);
  }
  return st;
}

int32_t
MochaRPCChannelRemoteId(MochaRPCChannel *channel, char **remoteId)
{
  StringLite tmp;
  int32_t st = channel->impl->remoteId(&tmp);
  if (st == MOCHA_RPC_OK) {
    size_t size;
    tmp.release(remoteId, &size);
  }
  return st;
}

int32_t
MochaRPCChannelResponse(MochaRPCChannel *channel, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return channel->impl->response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return channel->impl->response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t
MochaRPCChannelRequest(MochaRPCChannel *channel, int64_t *id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return channel->impl->request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return channel->impl->request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t
MochaRPCChannelRequest2(MochaRPCChannel *channel, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  int64_t discarded;
  return MochaRPCChannelRequest(channel, &discarded, code, headers, payload, payloadSize);
}

void MochaRPCChannelSetAttachment(MochaRPCChannel *channel, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor destructor)
{
  destroyAttachment(channel);
  channel->attachment = attachment;
  channel->attachmentDestructor = destructor;
}

MochaRPCOpaqueData
MochaRPCChannelGetAttachment(MochaRPCChannel *channel)
{
  return channel->attachment;
}
