#define __STDC_FORMAT_MACROS
#include <moca/rpc-c/RPCChannel.h>
#include <moca/rpc-c/RPCDispatcher.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <inttypes.h>
#include <stdio.h>

int32_t established = 0;

void eventListener(MocaRPCChannel *channel, int32_t eventType, MocaRPCOpaqueData eventData, MocaRPCOpaqueData userData);

void eventListener(MocaRPCChannel *channel, int32_t eventType, MocaRPCOpaqueData eventData, MocaRPCOpaqueData userData)
{
  char *remoteAddress = NULL, *localAddress = NULL, *remoteId = NULL, *localId = NULL;
  uint16_t remotePort, localPort;
  if (eventType != MOCA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED && eventType != MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED) {
    if (MocaRPCChannelRemoteAddress(channel, &remoteAddress, &remotePort) ||
        MocaRPCChannelLocalAddress(channel, &localAddress, &localPort)) {
      printf("Could not get address\n");
      goto cleanupExit;
    }
    if (MocaRPCChannelRemoteId(channel, &remoteId) ||
        MocaRPCChannelLocalId(channel, &localId)) {
      printf("Could not get id\n");
      goto cleanupExit;
    }
  }
  switch (eventType) {
    case MOCA_RPC_EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected to server %s@%s:%u from %s@%s:%u\n", remoteId, remoteAddress, remotePort, localId, localAddress, localPort);
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_ESTABLISHED:
      printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId, remoteAddress, remotePort, localId, localAddress, localPort);
      established = 1;
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Disconnected from server\n");
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
      {
        MocaRPCRequestEventData *data = (MocaRPCRequestEventData *) eventData;
        printf("Request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Code: %d\n", data->code);
        int32_t idx;
        for (idx= 0; idx < data->headers->size; ++idx) {
          MocaRPCKeyValuePair *pair = data->headers->pair[idx];
          printf("Header %s => %s\n", pair->key->content.ptr, pair->value->content.ptr);
        }
        printf("%d bytes payload\n", data->payloadSize);
        MocaRPCChannelResponse(channel, data->id, data->code - 100, data->headers, NULL, 0);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
      {
        MocaRPCResponseEventData *data = (MocaRPCResponseEventData *) eventData;
        printf("Response %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Code: %d\n", data->code);
        int32_t idx;
        for (idx= 0; idx < data->headers->size; ++idx) {
          MocaRPCKeyValuePair *pair = data->headers->pair[idx];
          printf("Header %s => %s\n", pair->key->content.ptr, pair->value->content.ptr);
        }
        printf("%d bytes payload\n", data->payloadSize);
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_PAYLOAD:
      {
        MocaRPCPayloadEventData *data = (MocaRPCPayloadEventData *) eventData;
        printf("Payload of request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId, remoteAddress, remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : %c\n", (*(char *) data->payload));
        printf("Commit : %s\n", data->commit ? "true" : "false");
      }
      break;
    case MOCA_RPC_EVENT_TYPE_CHANNEL_ERROR:
      {
        MocaRPCErrorEventData *data = (MocaRPCErrorEventData *) eventData;
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
  MocaRPCDispatcherBuilder *dispatcherBuilder = MocaRPCDispatcherBuilderCreate();
  MocaRPCDispatcher *dispatcher;
  if (MocaRPCDispatcherBuilderBuild(dispatcherBuilder, &dispatcher)) {
    printf("Could not build dispatcher");
    return 1;
  }
  MocaRPCDispatcherBuilderDestroy(dispatcherBuilder);
  MocaRPCDispatcherThread *dispatcherThread = MocaRPCDispatcherThreadCreate(dispatcher);
  MocaRPCChannelBuilder *builder = MocaRPCChannelBuilderCreate();
  MocaRPCChannel *client = NULL;
  
  if (MocaRPCChannelBuilderBuild(MocaRPCChannelBuilderDispatcher(MocaRPCChannelBuilderListener(
      MocaRPCChannelBuilderConnect(builder, argv[1]), eventListener, NULL, 0xFFFFFFF), dispatcher), &client)) {
    printf("Could not build client");
    return 1;
  }
  MocaRPCChannelBuilderDestroy(builder);

  sleep(10);
  MocaRPCChannelClose(client);
  MocaRPCChannelRelease(client);
  MocaRPCDispatcherStop(dispatcher);
  MocaRPCDispatcherRelease(dispatcher);
  MocaRPCDispatcherThreadShutdown(dispatcherThread);
  MocaRPCDispatcherThreadJoin(dispatcherThread);
  MocaRPCDispatcherThreadRelease(dispatcherThread);

  return 0;
}
