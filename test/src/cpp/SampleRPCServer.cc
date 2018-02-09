#include <mocha/rpc/RPCServer.h>
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

RPCDispatcher *dispatcher;

void asyncEventListener(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  StringLite remoteAddress;
  uint16_t remotePort;
  StringLite localAddress;
  uint16_t localPort;
  StringLite remoteId;
  StringLite localId;
  if (channel) {
    channel->remoteAddress(&remoteAddress, &remotePort);
    channel->localAddress(&localAddress, &localPort);
    channel->remoteId(&remoteId);
    channel->localId(&localId);
  }
  //printf("handle async event %d inside %s thread\n", eventType, dispatcher->isDispatchingThread() ? "dispatching" : "pooled");
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
        //printf(INT64_FMT"\n", data->id);
        /*
        printf("Request " INT64_FMT " from client %s@%s:%u\n", data->id, remoteId.str(), remoteAddress.str(), remotePort);
        printf("Code: %d\n", data->code);
        for (size_t idx = 0, size = data->headers->size(); idx < size; ++idx) {
          const KeyValuePair<StringLite, StringLite> *pair = data->headers->get(idx);
          printf("Header %s => %s\n", pair->key.str(), pair->value.str());
        }
        printf("%d bytes payload\n", data->payloadSize);
        */
        channel->response(data->id, data->code - 100, data->headers, "payload from server", sizeof("payload from server"));
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
        printf("Payload : %s\n", data->payload);
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

void eventListener(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  //printf("handle event %d inside %s thread\n", eventType, dispatcher->isDispatchingThread() ? "dispatching" : "pooled");
  server->submitAsync(channel, eventType, eventData, asyncEventListener, userData);
}

int main(int argc, const char **argv)
{
  signal(SIGPIPE, SIG_IGN);
  auto dispatcherBuilder = RPCDispatcher::newBuilder();
  dispatcher = dispatcherBuilder->build();
  auto dispatcherThread = RPCDispatcherThread::create(dispatcher);
  delete dispatcherBuilder;
  auto builder = RPCServer::newBuilder();
  auto server = builder->bind(argv[1])->listener(eventListener, NULL)->dispatcher(dispatcher)->threadPoolSize(2)->flags(RPCChannel::CHANNEL_FLAG_BLOCKING)->keepalive(1000000)->timeout(1000000000)->build();
  delete builder;

  sleep(argc > 2 ? strtoll(argv[2], NULL, 10) : 10);
  server->shutdown();
  dispatcherThread->shutdown();
  dispatcherThread->join();
  dispatcherThread->release();
  dispatcher->release();
  server->release();
  return 0;
}
