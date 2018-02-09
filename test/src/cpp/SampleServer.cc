#include <mocha/rpc/RPCChannel.h>
#include <mocha/rpc/RPCDispatcher.h>
#include <thread>
#include <unistd.h>
#include <signal.h>

using namespace mocha::rpc;
using namespace std;

#ifdef __DARWIN__
#define INT64_FMT "%lld"
#else
#define INT64_FMT "%ld"
#endif

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
    case EVENT_TYPE_CHANNEL_LISTENING:
      printf("Server is listening on %s@%s:%u\n", localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected from client %s@%s:%u to %s@%s:%u\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      printf("Session from client %s@%s:%u to %s@%s:%u is established\n", remoteId.str(), remoteAddress.str(), remotePort, localId.str(), localAddress.str(), localPort);
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Client is disconnected\n");
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        printf("Request " INT64_FMT " from client %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
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
        printf("Response " INT64_FMT " from client %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
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
        printf("Payload of request " INT64_FMT " from client %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
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

void testTimer(RPCDispatcher *dispatcher, RPCDispatcher::Timer *timer, int32_t events, RPCOpaqueData userData)
{
  if (events == RPCDispatcher::TIMER_FIRED) {
    dispatcher->destroy(timer);
  }
}

int main(int argc, const char **argv)
{
  signal(SIGPIPE, SIG_IGN);
  auto dispatcherBuilder = RPCDispatcher::newBuilder();
  auto dispatcher = dispatcherBuilder->build();
  auto dispatcherThread = RPCDispatcherThread::create(dispatcher);
  dispatcher->release();
  delete dispatcherBuilder;
  auto builder = RPCChannel::newBuilder();
  auto channel = builder->bind(argv[1])->listener(eventListener, NULL, 0xFFFFFFFF)->dispatcher(dispatcher)->flags(RPCChannel::CHANNEL_FLAG_BLOCKING)->build();
  delete builder;
  int32_t sleepTime = 60;
  RPCDispatcher::Timer *timer;

  dispatcher->create(&timer, 0, 1000000, testTimer);

  if (argc > 2) {
    sleepTime = strtoll(argv[2], NULL, 10);
  }
  sleep(sleepTime);
  channel->close();
  channel->release();
  dispatcherThread->shutdown();
  dispatcherThread->join();
  dispatcherThread->release();
  return 0;
}
