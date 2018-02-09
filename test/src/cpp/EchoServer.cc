#include <mocha/rpc/RPCServer.h>
#include <mocha/rpc/RPCDispatcher.h>
#include <thread>
#include <unistd.h>
#include <signal.h>

using namespace mocha::rpc;
using namespace std;

RPCDispatcher *dispatcher;

void eventListener(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_LISTENING:
      printf("Server is listening\n");
      break;
    case EVENT_TYPE_CHANNEL_CONNECTED:
      printf("Connected from client\n");
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      printf("Session from client is established\n");
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      printf("Channel is disconnected\n");
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
      {
        RequestEventData *data = static_cast<RequestEventData *>(eventData);
        channel->response(data->id, data->code - 100, data->headers, "payload from server", sizeof("payload from server"));
      }
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      break;
  }
}

int main(int argc, const char **argv)
{
  auto dispatcherBuilder = RPCDispatcher::newBuilder();
  dispatcher = dispatcherBuilder->build();
  auto dispatcherThread = RPCDispatcherThread::create(dispatcher);
  delete dispatcherBuilder;
  auto builder = RPCServer::newBuilder();
  auto server = builder->bind(argv[1])->listener(eventListener, NULL)->dispatcher(dispatcher)->threadPoolSize(300)->flags(RPCChannel::CHANNEL_FLAG_BLOCKING)->keepalive(1000000)->timeout(10000000)->connectionLimitPerIp(10000000)->connectRateLimitPerIp(100000000)->build();
  delete builder;

  if (server) {
    sleep(argc > 2 ? strtoll(argv[2], NULL, 10) : 10);
    server->shutdown();
  }
  dispatcherThread->shutdown();
  dispatcherThread->join();
  dispatcherThread->release();
  dispatcher->release();
  if (server) {
    server->release();
  }
  return 0;
}
