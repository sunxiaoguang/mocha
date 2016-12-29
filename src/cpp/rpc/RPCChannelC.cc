#include "RPCChannelC.h"

struct MocaRPCChannelBuilder
{
  RPCChannel::Builder *impl;
  MocaRPCChannel *channel;
};

static void MocaRPCEventListenerAdapter(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MocaRPCChannel *wrapper = static_cast<MocaRPCChannel *>(userData);
  MocaRPCChannel *attachment = static_cast<MocaRPCChannel *>(channel->attachment());
  switch (eventType) {
    case MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED:
      if (attachment == NULL) {
        channel->attachment((attachment = static_cast<MocaRPCChannel *>(calloc(1, sizeof(MocaRPCChannel)))), NULL);
      }
      wrapper->refcount++;
      attachment->impl = channel;
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED) {
        wrapper->listener(attachment, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED:
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
        wrapper->listener(attachment, eventType, eventData, wrapper->userData);
      }
      if (attachment != wrapper) {
        destroy(attachment);
      }
      if (--wrapper->refcount == 0) {
        destroy(wrapper);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
      /* FALL THROUGH */
    case MOCA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
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

MocaRPCChannelBuilder *
MocaRPCChannelBuilderCreate(void)
{
  MocaRPCChannel *channel = static_cast<MocaRPCChannel *>(calloc(1, sizeof(MocaRPCChannel)));
  MocaRPCChannelBuilder *builder = static_cast<MocaRPCChannelBuilder *>(calloc(1, sizeof(MocaRPCChannelBuilder)));
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

MocaRPCChannelBuilder *
MocaRPCChannelBuilderBind(MocaRPCChannelBuilder *builder, const char *address)
{
  builder->impl->bind(address);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderConnect(MocaRPCChannelBuilder *builder, const char *address)
{
  builder->impl->connect(address);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderTimeout(MocaRPCChannelBuilder *builder, int64_t timeout)
{
  builder->impl->timeout(timeout);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderLimit(MocaRPCChannelBuilder *builder, int32_t size)
{
  builder->impl->limit(size);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderListener(MocaRPCChannelBuilder *builder, MocaRPCEventListener listener, MocaRPCOpaqueData userData, int32_t eventMask)
{
  builder->channel->listener = listener;
  builder->channel->userData = userData;
  builder->channel->eventMask = eventMask;
  builder->impl->listener(MocaRPCEventListenerAdapter, builder->channel, eventMask | MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED | MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderKeepalive(MocaRPCChannelBuilder *builder, int64_t interval)
{
  builder->impl->keepalive(interval);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderId(MocaRPCChannelBuilder *builder, const char *id)
{
  builder->impl->id(id);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderLogger(MocaRPCChannelBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData userData)
{
  builder->impl->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), userData);
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderDispatcher(MocaRPCChannelBuilder *builder, MocaRPCDispatcher *dispatcher)
{
  builder->impl->dispatcher(reinterpret_cast<RPCDispatcher *>(dispatcher));
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderAttachment(MocaRPCChannelBuilder *builder, MocaRPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  builder->channel->attachment = attachment;
  builder->channel->attachmentDestructor = attachmentDestructor;
  return builder;
}

MocaRPCChannelBuilder *
MocaRPCChannelBuilderFlags(MocaRPCChannelBuilder *builder, int32_t flags)
{
  builder->impl->flags(flags);
  return builder;
}

int32_t
MocaRPCChannelBuilderBuild(MocaRPCChannelBuilder *builder, MocaRPCChannel **channel)
{
  builder->channel->impl = builder->impl->build();

  int32_t st;
  if (builder->channel->impl == NULL) {
    st = MOCA_RPC_INTERNAL_ERROR;
  } else {
    st = MOCA_RPC_OK;
  }
  *channel = builder->channel;
  builder->channel = static_cast<MocaRPCChannel *>(calloc(1, sizeof(MocaRPCChannel)));
  return st;
}

void
MocaRPCChannelBuilderDestroy(MocaRPCChannelBuilder *builder)
{
  delete builder->impl;
  destroy(builder->channel);
  free(builder);
}

void
MocaRPCChannelAddRef(MocaRPCChannel *channel)
{
  channel->impl->addRef();
}

int32_t
MocaRPCChannelRelease(MocaRPCChannel *channel)
{
  return channel->impl->release();
}

void
MocaRPCChannelClose(MocaRPCChannel *channel)
{
  channel->impl->close();
}

int32_t
MocaRPCChannelLocalAddress(MocaRPCChannel *channel, char **localAddress, uint16_t *port)
{
  StringLite tmp;
  int32_t st = channel->impl->localAddress(&tmp, port);
  if (st == MOCA_RPC_OK) {
    size_t size;
    tmp.release(localAddress, &size);
  }
  return st;
}

int32_t
MocaRPCChannelRemoteAddress(MocaRPCChannel *channel, char **remoteAddress, uint16_t *port)
{
  StringLite tmp;
  int32_t st = channel->impl->remoteAddress(&tmp, port);
  if (st == MOCA_RPC_OK) {
    size_t size;
    tmp.release(remoteAddress, &size);
  }
  return st;
}

int32_t
MocaRPCChannelLocalId(MocaRPCChannel *channel, char **localId)
{
  StringLite tmp;
  int32_t st = channel->impl->localId(&tmp);
  if (st == MOCA_RPC_OK) {
    size_t size;
    tmp.release(localId, &size);
  }
  return st;
}

int32_t
MocaRPCChannelRemoteId(MocaRPCChannel *channel, char **remoteId)
{
  StringLite tmp;
  int32_t st = channel->impl->remoteId(&tmp);
  if (st == MOCA_RPC_OK) {
    size_t size;
    tmp.release(remoteId, &size);
  }
  return st;
}

int32_t
MocaRPCChannelResponse(MocaRPCChannel *channel, int64_t id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
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
MocaRPCChannelRequest(MocaRPCChannel *channel, int64_t *id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
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
MocaRPCChannelRequest2(MocaRPCChannel *channel, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  int64_t discarded;
  return MocaRPCChannelRequest(channel, &discarded, code, headers, payload, payloadSize);
}

void MocaRPCChannelSetAttachment(MocaRPCChannel *channel, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor destructor)
{
  destroyAttachment(channel);
  channel->attachment = attachment;
  channel->attachmentDestructor = destructor;
}

MocaRPCOpaqueData
MocaRPCChannelGetAttachment(MocaRPCChannel *channel)
{
  return channel->attachment;
}
