#include <mocha/rpc-c/RPCServer.h>
#include <mocha/rpc/RPCServer.h>
#include "RPCLite.h"
#include "RPCServer.h"
#include "RPCChannelC.h"

using namespace mocha::rpc;

struct MochaRPCServer : MochaRPCWrapper
{
  RPCServer *impl;
  MochaRPCServerEventListener listener;
  MochaRPCOpaqueData userData;
  int32_t eventMask;
  MochaRPCChannel channel;
};

struct MochaRPCServerBuilder
{
  RPCServer::Builder *impl;
  MochaRPCServer *server;
};

static void MochaRPCServerEventListenerAdapter(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MochaRPCServer *wrapper = static_cast<MochaRPCServer *>(userData);
  MochaRPCChannel *channelWrapper = channel ? channel->attachment<MochaRPCChannel>() : NULL;
  switch (eventType) {
    case MOCHA_RPC_EVENT_TYPE_SERVER_CREATED:
      wrapper->impl = server;
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_SERVER_CREATED) {
        wrapper->listener(wrapper, NULL, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED:
      if (wrapper->channel.impl == NULL) {
        channelWrapper = &wrapper->channel;
      } else {
        channelWrapper = static_cast<MochaRPCChannel *>(calloc(1, sizeof(MochaRPCChannel)));
      }
      channelWrapper->impl = channel;
      channel->attachment(channelWrapper, NULL);
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED) {
        wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED:
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
        wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      }
      if (channelWrapper != &wrapper->channel) {
        free(channelWrapper);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_SERVER_DESTROYED:
      if (wrapper->eventMask & MOCHA_RPC_EVENT_TYPE_SERVER_DESTROYED) {
        wrapper->listener(wrapper, NULL, eventType, eventData, wrapper->userData);
      }
      destroy(wrapper);
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
      wrapper->listener(wrapper, channelWrapper, eventType, eventData, wrapper->userData);
      break;
  }
}

MochaRPCServerBuilder *
MochaRPCServerBuilderCreate(void)
{
  MochaRPCServer *server = static_cast<MochaRPCServer *>(calloc(1, sizeof(MochaRPCServer)));
  MochaRPCServerBuilder *builder = static_cast<MochaRPCServerBuilder *>(calloc(1, sizeof(MochaRPCServerBuilder)));
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

MochaRPCServerBuilder *
MochaRPCServerBuilderBind(MochaRPCServerBuilder *builder, const char *address)
{
  builder->impl->bind(address);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderTimeout(MochaRPCServerBuilder *builder, int64_t timeout)
{
  builder->impl->timeout(timeout);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderHeaderLimit(MochaRPCServerBuilder *builder, int32_t size)
{
  builder->impl->headerLimit(size);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderPayloadLimit(MochaRPCServerBuilder *builder, int32_t size)
{
  builder->impl->payloadLimit(size);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderListener(MochaRPCServerBuilder *builder, MochaRPCServerEventListener listener, MochaRPCOpaqueData userData, int32_t eventMask)
{
  builder->server->listener = listener;
  builder->server->userData = userData;
  builder->server->eventMask = eventMask;
  builder->impl->listener(MochaRPCServerEventListenerAdapter, builder->server,
      eventMask | MOCHA_RPC_EVENT_TYPE_CHANNEL_CREATED | MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED | MOCHA_RPC_EVENT_TYPE_SERVER_CREATED | MOCHA_RPC_EVENT_TYPE_SERVER_DESTROYED);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderKeepalive(MochaRPCServerBuilder *builder, int64_t interval)
{
  builder->impl->keepalive(interval);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderId(MochaRPCServerBuilder *builder, const char *id)
{
  builder->impl->id(id);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderLogger(MochaRPCServerBuilder *builder, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData userData)
{
  builder->impl->logger(reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), userData);
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderDispatcher(MochaRPCServerBuilder *builder, MochaRPCDispatcher *dispatcher)
{
  builder->impl->dispatcher(reinterpret_cast<RPCDispatcher *>(dispatcher));
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderAttachment(MochaRPCServerBuilder *builder, MochaRPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor)
{
  builder->server->attachment = attachment;
  builder->server->attachmentDestructor = attachmentDestructor;
  return builder;
}

MochaRPCServerBuilder *
MochaRPCServerBuilderFlags(MochaRPCServerBuilder *builder, int32_t flags)
{
  builder->impl->flags(flags);
  return builder;
}

int32_t
MochaRPCServerBuilderBuild(MochaRPCServerBuilder *builder, MochaRPCServer **server)
{
  builder->server->impl = builder->impl->build();

  int32_t st;
  if (builder->server->impl == NULL) {
    st = MOCHA_RPC_INTERNAL_ERROR;
  } else {
    st = MOCHA_RPC_OK;
  }
  *server = builder->server;
  builder->server = static_cast<MochaRPCServer *>(calloc(1, sizeof(MochaRPCServer)));
  return st;
}

void
MochaRPCServerBuilderDestroy(MochaRPCServerBuilder *builder)
{
  delete builder->impl;
  destroy(builder->server);
  free(builder);
}

void
MochaRPCServerAddRef(MochaRPCServer *server)
{
  server->impl->addRef();
}

int32_t
MochaRPCServerRelease(MochaRPCServer *server)
{
  return server->impl->release();
}

void
MochaRPCServerShutdown(MochaRPCServer *server)
{
  server->impl->shutdown();
}

void MochaRPCServerSetAttachment(MochaRPCServer *server, MochaRPCOpaqueData attachment, MochaRPCOpaqueDataDestructor destructor)
{
  destroyAttachment(server);
  server->attachment = attachment;
  server->attachmentDestructor = destructor;
}

MochaRPCOpaqueData
MochaRPCServerGetAttachment(MochaRPCServer *server)
{
  return server->attachment;
}
