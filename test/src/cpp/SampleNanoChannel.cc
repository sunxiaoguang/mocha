#define __STDC_FORMAT_MACROS
#include <moca/rpc/RPCChannelNano.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

using namespace moca::rpc;

RPCChannelNano *client;

void eventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  if (eventType == EVENT_TYPE_CHANNEL_ERROR) {
    ErrorEventData *data = static_cast<ErrorEventData *>(eventData);
    printf("Error %d:%s\n", data->code, data->message);
    return;
  }
  if (eventType == EVENT_TYPE_CHANNEL_DISCONNECTED) {
    printf("Disconnected from server\n");
    return;
  }
  StringLite remoteAddress;
  uint16_t remotePort;
  channel->remoteAddress(&remoteAddress, &remotePort);
  StringLite localAddress;
  uint16_t localPort;
  channel->localAddress(&localAddress, &localPort);
  StringLite remoteId;
  channel->remoteId(&remoteId);
  StringLite localId;
  channel->localId(&localId);
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected to server %s@%s:%u from %s@%s:%u\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      {
        KeyValuePairs<StringLite, StringLite> headers;
        StringLite k("key");
        StringLite v("value");
        headers.append(k, v);
        int64_t id = 0;
        channel->request(&id, 0, &headers);
      }
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
        channel->response(data->id, data->code - 100, data->headers, NULL, 0);
      }
      break;
    case EVENT_TYPE_CHANNEL_RESPONSE:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Response %" PRId64 " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
      }
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      {
        PayloadEventData *data = static_cast<PayloadEventData *>(eventData);
        printf("Payload of request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : %s\n", data->payload);
        printf("Commit : %s\n", data->commit ? "true" : "false");
        client->breakLoop();
      }
      break;
  }
}

void *breakerEntry(void *arg)
{
  RPCChannelNano *client = static_cast<RPCChannelNano *>(arg);
  while (true) {
    sleep(10);
    printf("break\n");
    client->breakLoop();
  }
  return NULL;
}

int main(int argc, char **argv)
{
  auto builder = RPCChannelNano::newBuilder();
  client = builder->connect(argv[1])->listener(eventListener, NULL)->timeout(1000000000l)->build();
  if (!client) {
    printf("Could not create\n");
    return 1;
  }
  client->loop();
  return 0;
}
