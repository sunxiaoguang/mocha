#include <mocha/rpc/RPCChannelNano.h>
#include <unistd.h>
#include <pthread.h>
#include <cinttypes>

using namespace mocha::rpc;
int64_t authRequestId = 0;

void eventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
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
    case EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected to server %s@%s:%u from %s@%s:%u\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      {
        printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
        KeyValuePairs<StringLite, StringLite> headers;
        RPCPacketHeaders::save(&headers, "u", RPCPacketHeaders::CSTRING, "1", "s", RPCPacketHeaders::CSTRING, "s", RPCPacketHeaders::CSTRING, "k", NULL);
        channel->request(&authRequestId, 1, &headers);
      }
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Disconnected from server\n");
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
        if (data->code == 4) {
          printf("Broadcast\n");
        } else {
          printf("Publish\n");
        }
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
        if (authRequestId == data->id) {
          KeyValuePairs<StringLite, StringLite> headers;
          headers.append("s", "0:10:21");
          headers.append("f", "");
          int64_t tmp;
          channel->request(&tmp, 2, &headers);
        }
      }
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      {
        PayloadEventData *data = static_cast<PayloadEventData *>(eventData);
        printf("Payload of request %" PRId64 " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : %" PRId64 "\n", (*(int64_t *) data->payload));
        printf("Commit : %s\n", data->commit ? "true" : "false");
      }
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      {
        ErrorEventData *data = static_cast<ErrorEventData *>(eventData);
        printf("Error %d:%s\n", data->code, data->message);
      }
      break;
  }
}

int main(int argc, char **argv)
{
  auto builder = RPCChannelNano::newBuilder();
  RPCChannelNano *client = builder->connect(argv[1])->listener(eventListener, NULL)->timeout(1000000000l)->build();
  if (!client) {
    printf("Could not create\n");
    return 1;
  }
  while (true) {
    client->loop();
  }
  return 0;
}
