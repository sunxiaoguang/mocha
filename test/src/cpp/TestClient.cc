#include "RPC.h"
#include <unistd.h>
#include <pthread.h>

using namespace moca::rpc;

void eventListener(const RPCClient *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
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
    case EVENT_TYPE_CONNECTED:
      printf("Connected to server %s@%s:%u from %s@%s:%u\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_ESTABLISHED:
      printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_DISCONNECTED:
      printf("Disconnected from server\n");
      break;
    case EVENT_TYPE_REQUEST:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Request %lld from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
        channel->response(data->id, data->code - 100, data->headers, NULL, 0);
      }
      break;
    case EVENT_TYPE_RESPONSE:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Response %lld from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
      }
      break;
    case EVENT_TYPE_PAYLOAD:
      {
        PayloadEventData *data = static_cast<PayloadEventData *>(eventData);
        printf("Payload of request %lld from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : %lld\n", (*(int64_t *) data->payload));
        printf("Commit : %s\n", data->commit ? "true" : "false");
      }
      break;
    case EVENT_TYPE_ERROR:
      {
        ErrorEventData *data = static_cast<ErrorEventData *>(eventData);
        printf("Error %d:%s\n", data->code, data->message.str());
      }
      break;
  }
}

void *breakerEntry(void *arg)
{
  RPCClient *client = static_cast<RPCClient *>(arg);
  while (true) {
    sleep(10);
    printf("break\n");
    client->breakLoop();
  }
  return NULL;
}

int main(int argc, char **argv)
{
  RPCClient *client = RPCClient::create(1000000000l);
  if (!client) {
    printf("Could not create\n");
    return 1;
  }
  client->addListener(eventListener);
  if (MOCA_RPC_FAILED(client->connect("192.168.80.41:1234"))) {
    printf("Could not connect\n");
    return 1;
  }
  KeyValuePairs<StringLite, StringLite> headers;
  StringLite k("key");
  StringLite v("value");
  headers.append(k, v);
  int64_t id = 0;
  client->request(&id, 0, &headers);
  //pthread_t breaker;
  //pthread_create(&breaker, NULL, breakerEntry, client);
  client->loop();
  return 0;
}
