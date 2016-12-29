#ifndef __MOCA_RPC_THREAD_POOL_INTERNAL_H__
#define __MOCA_RPC_THREAD_POOL_INTERNAL_H__ 1

#include "RPC.h"
#include "uv.h"

BEGIN_MOCA_RPC_NAMESPACE

class RPCThreadPool : private RPCNonCopyable
{
private:
  struct RPCWorker {
    volatile int32_t flags;
    int32_t id;
    RPCAsyncQueue *asyncQueue;
    uv_thread_t thread;
  };

private:
  int32_t size_;
  RPCWorker *workers_;
  RPCAtomic<int64_t> counter_;
  uv_mutex_t mutex_;
  volatile bool running_;
  RPCAsyncQueue asyncQueue_;

private:
  int32_t initWorker(int32_t id, RPCWorker *worker);
  static void workerEntry(void *arg);
  static void fireAsyncTask(RPCAsyncTask task, RPCOpaqueData taskUserData, RPCOpaqueData sinkUserData);

  inline bool isRunning();
  inline RPCWorker *worker(int32_t index) { return workers_ + index; }

public:
  RPCThreadPool(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  ~RPCThreadPool();
  int32_t init(int32_t size);

  int32_t submit(RPCAsyncTask task, RPCOpaqueData data);
  int32_t submit(RPCAsyncTask task, RPCOpaqueData data, int32_t key);

  int32_t shutdown();
};

END_MOCA_RPC_NAMESPACE

#endif /* __MOCA_RPC_THREAD_POOL_INTERNAL_H__*/
