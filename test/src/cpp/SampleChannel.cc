#include <moca/rpc/RPCChannel.h>
#include <moca/rpc/RPCDispatcher.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <thread>

using namespace moca::rpc;

#ifdef __DARWIN__
#define INT64_FMT "%lld"
#else
#define INT64_FMT "%ld"
#endif

volatile int32_t established = 0;
volatile bool running = true;
volatile bool disconnected = false;

void eventListener(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
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
      printf("Session to server %s@%s:%u from %s@%s:%u is established\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      established = 1;
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Disconnected from server\n");
      disconnected = true;
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Request " INT64_FMT " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
        channel->response(data->id, data->code - 100, data->headers, NULL, 0);
        channel->response(data->id + 1, data->code - 100, data->headers, "abc", 3);
      }
      break;
    case EVENT_TYPE_CHANNEL_RESPONSE:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Response " INT64_FMT " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
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
        printf("Payload of request " INT64_FMT " from server %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Size : %d\n", data->size);
        printf("Payload : " INT64_FMT "\n", (*(int64_t *) data->payload));
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
  signal(SIGPIPE, SIG_IGN);
  auto dispatcherBuilder = RPCDispatcher::newBuilder();
  auto dispatcher = dispatcherBuilder->build();
  delete dispatcherBuilder;
  thread thr([&] () -> void {
    while (running) {
      dispatcher->run();
      sleep(1);
      printf("Returned from run\n");
    }
  });
  auto builder = RPCChannel::newBuilder();
  auto channel = builder->connect(argv[1])->listener(eventListener, NULL, 0xFFFFFFFF)->keepalive(1000000)->dispatcher(dispatcher)->flags(RPCChannel::CHANNEL_FLAG_BLOCKING)->build();
  delete builder;

  KeyValuePairs<StringLite, StringLite> headers;
  StringLite k("key");
  StringLite v("value");
  headers.append(k, v);
  while (!established && !disconnected) {
    sleep(1);
  }
  if (!disconnected) {
    int64_t id = 0;
    channel->request(&id, 0, &headers);
    sleep(argc > 2 ? strtol(argv[2], NULL, 10) : 0);
  }
  channel->close();
  channel->release();
  dispatcher->stop();
  running = false;
  thr.join();
  dispatcher->release();
  return 0;
}
