#include <mocha/rpc/RPCChannel.h>
#include <mocha/rpc/RPCDispatcher.h>
#include <stdint.h>
#include <unistd.h>

using namespace mocha::rpc;

void testTimer(RPCDispatcher *dispatcher, RPCDispatcher::Timer *timer, int32_t events, RPCOpaqueData userData)
{
}

void asyncTask(RPCDispatcher *dispatcher, RPCOpaqueData userData)
{
  printf("async task %p@%p\n", dispatcher, userData);
}


int32_t main(int32_t argc, const char **argv)
{
  auto dispatcherBuilder = RPCDispatcher::newBuilder();
  auto dispatcher = dispatcherBuilder->build();
  auto dispatcherThread = RPCDispatcherThread::create(dispatcher);
  dispatcher->release();
  delete dispatcherBuilder;

  int64_t counter = 0;
  while (true) {
    RPCDispatcher::Timer *timer;
    if (dispatcher->create(&timer, 0, 100000000, testTimer) == 0) {
      dispatcher->destroy(timer);
    }
    dispatcher->submitAsync(asyncTask, (void *)(counter++));
  }

  while (true) {
    sleep(1000000000);
  }

  dispatcherThread->shutdown();
  dispatcherThread->join();
  dispatcherThread->release();

  return 0;
}
