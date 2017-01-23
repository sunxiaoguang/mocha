#include "RPC.h"
#include "RPCThreadPool.h"
#include "RPCLogging.h"
#include <unistd.h>

using namespace moca::rpc;

RPCThreadPool *pool;
const int32_t count = 1000000;
RPCAtomic<int32_t> counter;

void dummy(RPCOpaqueData data)
{
  counter.add(1);
}

int32_t main(int32_t argc, char **argv)
{
  pool = new RPCThreadPool(defaultRPCLogger, defaultRPCLoggerLevel, defaultRPCLoggerUserData);
  int32_t st;
  if (MOCA_RPC_FAILED(st = pool->init(4))) {
    return 1;
  }

  for (int32_t idx = 0; idx < count; ++idx) {
    pool->submit(dummy, NULL);
  }

  do {
    sleep(1);
  } while (counter.get() < count);

  return 0;
}
