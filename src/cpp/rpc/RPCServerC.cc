#include <moca/rpc-c/RPCServer.h>
#include <moca/rpc/RPCServer.h>
#include "RPCLite.h"
#include "RPCServer.h"
#include "RPCChannelC.h"

using namespace moca::rpc;

struct MocaRPCServer : MocaRPCWrapper
{
  RPCServer *impl;
  MocaRPCServerEventListener listener;
  MocaRPCOpaqueData userData;
  int32_t eventMask;
  MocaRPCChannel channel;
};

struct MocaRPCServerBuilder
{
  RPCServer::Builder *impl;
  MocaRPCServer *server;
};

static void MocaRPCServerEventListenerAdapter(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MocaRPCServer *wrapper = static_cast<MocaRPCServer *>(userData);
  MocaRPCChannel *channelWrapper = channel ? channel->attachment<MocaRPCChannel>() : NULL;
  switch (eventType) {
    case MOCA_RPC_EVENT_TYPE_SERVER_CREATED:
      wrapper->impl = server;
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_SERVER_CREATED) {
        wrapper->listener(wrapper, NULL, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED:
      if (wrapper->channel.impl == NULL) {
        channelWrapper = &wrapper->channel;
      } else {
        channelWrapper = static_cast<MocaRPCChannel *>(calloc(1, sizeof(MocaRPCChannel)));
      }
      channelWrapper->impl = channel;
      channel->attachment(channelWrapper, NULL);
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED) {
        wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED:
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
        wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      }
      if (channelWrapper != &wrapper->channel) {
        free(channelWrapper);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED:
      if (wrapper->eventMask & MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED) {
        wrapper->listener(wrapper, NULL, eventType, eventData, wrapper->userData);
      }
      destroy(wrapper);
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
      wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      break;
  }
}

MocaRPCServerBuilder *
MocaRPCServerBuilderCreate(void)
{
  MocaRPCServer *server = static_cast<MocaRPCServer *>(calloc(1, sizeof(MocaRPCServer)));
  MocaRPCServerBuilder *builder = static_cast<MocaRPCServerBuilder *>(calloc(1, sizeof(MocaRPCServerBuilder)));
  if (server == NULL || builder == NULL) {
    goto cleanupExit;
  }
  builder->impl = RPCServer::newBuilder();
  if (builder->impl == NULL) {
    goto cleanupExit;
  }
  builder->server = server;
  return builder;
cleanupExit:
  destroy(server);
  free(builder);
  return NULL;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderBind(MocaRPCServerBuilder *builder, const char *address)
{
  builder->impl->bind(address);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderTimeout(MocaRPCServerBuilder *builder, int64_t timeout)
{
  builder->impl->timeout(timeout);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderHeaderLimit(MocaRPCServerBuilder *builder, int32_t size)
{
  builder->impl->headerLimit(size);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderPayloadLimit(MocaRPCServerBuilder *builder, int32_t size)
{
  builder->impl->payloadLimit(size);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderListener(MocaRPCServerBuilder *builder, MocaRPCServerEventListener listener, MocaRPCOpaqueData userData, int32_t eventMask)
{
  builder->server->listener = listener;
  builder->server->userData = userData;
  builder->server->eventMask = eventMask;
  builder->impl->listener(MocaRPCServerEventListenerAdapter, builder->server,
      eventMask | MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED | MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED | MOCA_RPC_EVENT_TYPE_SERVER_CREATED | MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderKeepalive(MocaRPCServerBuilder *builder, int64_t interval)
{
  builder->impl->keepalive(interval);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderId(MocaRPCServerBuilder *builder, const char *id)
{
  builder->impl->id(id);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderLogger(MocaRPCServerBuilder *builder, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData userData)
{
  builder->impl->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), userData);
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderDispatcher(MocaRPCServerBuilder *builder, MocaRPCDispatcher *dispatcher)
{
  builder->impl->dispatcher(reinterpret_cast<RPCDispatcher *>(dispatcher));
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderAttachment(MocaRPCServerBuilder *builder, MocaRPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  builder->server->attachment = attachment;
  builder->server->attachmentDestructor = attachmentDestructor;
  return builder;
}

MocaRPCServerBuilder *
MocaRPCServerBuilderFlags(MocaRPCServerBuilder *builder, int32_t flags)
{
  builder->impl->flags(flags);
  return builder;
}

int32_t
MocaRPCServerBuilderBuild(MocaRPCServerBuilder *builder, MocaRPCServer **server)
{
  builder->server->impl = builder->impl->build();

  int32_t st;
  if (builder->server->impl == NULL) {
    st = MOCA_RPC_INTERNAL_ERROR;
  } else {
    st = MOCA_RPC_OK;
  }
  *server = builder->server;
  builder->server = static_cast<MocaRPCServer *>(calloc(1, sizeof(MocaRPCServer)));
  return st;
}

void
MocaRPCServerBuilderDestroy(MocaRPCServerBuilder *builder)
{
  delete builder->impl;
  destroy(builder->server);
  free(builder);
}

void
MocaRPCServerAddRef(MocaRPCServer *server)
{
  server->impl->addRef();
}

int32_t
MocaRPCServerRelease(MocaRPCServer *server)
{
  return server->impl->release();
}

void
MocaRPCServerShutdown(MocaRPCServer *server)
{
  server->impl->shutdown();
}

void MocaRPCServerSetAttachment(MocaRPCServer *server, MocaRPCOpaqueData attachment, MocaRPCOpaqueDataDestructor destructor)
{
  destroyAttachment(server);
  server->attachment = attachment;
  server->attachmentDestructor = destructor;
}

MocaRPCOpaqueData
MocaRPCServerGetAttachment(MocaRPCServer *server)
{
  return server->attachment;
}
