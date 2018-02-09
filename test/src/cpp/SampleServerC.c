#define __STDC_FORMAT_MACROS
#include <mocha/rpc-c/RPCChannel.h>
#include <mocha/rpc-c/RPCDispatcher.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <inttypes.h>

int32_t established = 0;

void eventListener(MochaRPCChannel *channel, int32_t eventType, MochaRPCOpaqueData eventData, MochaRPCOpaqueData userData);

void eventListener(MochaRPCChannel *channel, int32_t eventType, MochaRPCOpaqueData eventData, MochaRPCOpaqueData userData)
{
  char *remoteAddress = NULL, *localAddress = NULL, *remoteId = NULL, *localId = NULL;
  uint16_t remotePort, localPort;
  if (eventType != MOCHA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED && eventType != MOCHA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
    if (MochaRPCChannelRemoteAddress(channel, &remoteAddress, &remotePort) ||
        MochaRPCChannelLocalAddress(channel, &localAddress, &localPort)) {
      printf("Could not get address\n");
      goto cleanupExit;
    }
    if (MochaRPCChannelRemoteId(channel, &remoteId) ||
        MochaRPCChannelLocalId(channel, &localId)) {
      printf("Could not get id\n");
      goto cleanupExit;
    }
  }
  switch (eventType) {
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected to server %s@%s:%u from %s@%s:%u\n", remoteId, remoteAddress, remotePort, localId, localAddress, localPort);
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_ESTABLISHED:
      printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId, remoteAddress, remotePort, localId, localAddress, localPort);
      established = 1;
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Disconnected from server\n");
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
      {
        MochaRPCRequestEventData *data = (MochaRPCRequestEventData *) eventData;
        printf("Request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Code: %d\n", data->code);
        int32_t idx;
        for (idx= 0; idx < data->headers->size; ++idx) {
          MochaRPCKeyValuePair *pair = data->headers->pair[idx];
          printf("Header %s => %s\n", pair->key->content.ptr, pair->value->content.ptr);
        }
        printf("%d bytes payload\n", data->payloadSize);
        MochaRPCChannelResponse(channel, data->id, data->code - 100, data->headers, NULL, 0);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
      {
        MochaRPCResponseEventData *data = (MochaRPCResponseEventData *) eventData;
        printf("Response %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Code: %d\n", data->code);
        int32_t idx;
        for (idx= 0; idx < data->headers->size; ++idx) {
          MochaRPCKeyValuePair *pair = data->headers->pair[idx];
          printf("Header %s => %s\n", pair->key->content.ptr, pair->value->content.ptr);
        }
        printf("%d bytes payload\n", data->payloadSize);
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_PAYLOAD:
      {
        MochaRPCPayloadEventData *data = (MochaRPCPayloadEventData *) eventData;
        printf("Payload of request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : %c\n", (*(char *) data->payload));
        printf("Commit : %s\n", data->commit ? "true" : "false");
      }
      break;
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_ERROR:
      {
        MochaRPCErrorEventData *data = (MochaRPCErrorEventData *) eventData;
        printf("Error %d:%s\n", data->code, data->message);
      }
      break;
  }

cleanupExit:
  free(remoteAddress);
  free(localAddress);
  free(remoteId);
  free(localId);
}

int main(int argc, char **argv)
{
  signal(SIGPIPE, SIG_IGN);
  MochaRPCDispatcherBuilder *dispatcherBuilder = MochaRPCDispatcherBuilderCreate();
  MochaRPCDispatcher *dispatcher;
  if (MochaRPCDispatcherBuilderBuild(dispatcherBuilder, &dispatcher)) {
    printf("Could not build dispatcher");
    return 1;
  }
  MochaRPCDispatcherBuilderDestroy(dispatcherBuilder);
  MochaRPCDispatcherThread *dispatcherThread = MochaRPCDispatcherThreadCreate(dispatcher);
  MochaRPCChannelBuilder *builder = MochaRPCChannelBuilderCreate();
  MochaRPCChannel *server = NULL;
  
  if (MochaRPCChannelBuilderBuild(MochaRPCChannelBuilderDispatcher(MochaRPCChannelBuilderListener(
      MochaRPCChannelBuilderBind(builder, argv[1]), eventListener, NULL, 0xFFFFFFF), dispatcher), &server)) {
    printf("Could not build server");
    return 1;
  }
  MochaRPCChannelBuilderDestroy(builder);

  sleep(10);
  MochaRPCChannelClose(server);
  MochaRPCChannelRelease(server);
  MochaRPCDispatcherStop(dispatcher);
  MochaRPCDispatcherRelease(dispatcher);
  MochaRPCDispatcherThreadShutdown(dispatcherThread);
  MochaRPCDispatcherThreadJoin(dispatcherThread);
  MochaRPCDispatcherThreadRelease(dispatcherThread);
  return 0;
}
