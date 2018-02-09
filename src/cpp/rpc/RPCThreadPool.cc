#include "RPCThreadPool.h"

BEGIN_MOCHA_RPC_NAMESPACE

RPCThreadPool::RPCThreadPool(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
  : size_(0), workers_(NULL), counter_(0), running_(true), asyncQueue_(logger, level, userData)
{
}

#define THREAD_INITIALIZED (1)
#define RUNNING (2)

int32_t
RPCThreadPool::init(int32_t size)
{
  int32_t st = RPC_OK;
  size_ = size;
  /* TODO make this configurable */
  asyncQueue_.init(size, 1024);
  workers_ = new RPCWorker[size_];
  for (int32_t idx = 0; idx < size_; ++idx) {
    if (MOCHA_RPC_FAILED(st = initWorker(idx, worker(idx)))) {
      return st;
    }
  }

  return st;
}

RPCThreadPool::~RPCThreadPool()
{
  delete[] workers_;
}

void
RPCThreadPool::fireAsyncTask(RPCAsyncTask task, RPCOpaqueData taskUserData, RPCOpaqueData sinkUserData)
{
  task(taskUserData);
}

RPCOpaqueData
RPCThreadPool::workerEntry(RPCOpaqueData arg)
{
  RPCWorker *worker = static_cast<RPCWorker *>(arg);

  while ((worker->flags & RUNNING) != 0) {
    worker->asyncQueue->dequeue(fireAsyncTask, NULL, worker->id, true);
  }
  return NULL;
}

bool
RPCThreadPool::isRunning()
{
  RPCLock lock(mutex_);
  return running_;
}

int32_t
RPCThreadPool::submit(RPCAsyncTask task, RPCOpaqueData data)
{
  if (!isRunning()) {
    return RPC_ILLEGAL_STATE;
  }
  return submit(task, data, static_cast<int32_t>(counter_.add(1) & 0x7FFFFFFF));
}

int32_t
RPCThreadPool::submit(RPCAsyncTask task, RPCOpaqueData data, int32_t key)
{
  if (!isRunning()) {
    return RPC_ILLEGAL_STATE;
  }
  return asyncQueue_.enqueue(task, data, key);
}

int32_t
RPCThreadPool::initWorker(int32_t id, RPCWorker *worker)
{
  worker->flags |= RUNNING;
  worker->id = id;
  worker->asyncQueue = &asyncQueue_;
  new (&worker->thread) RPCThread(workerEntry, worker);
  if (MOCHA_RPC_FAILED(worker->thread.start())) {
    return RPC_INTERNAL_ERROR;
  }
  worker->flags |= THREAD_INITIALIZED;
  return RPC_OK;
}

int32_t
RPCThreadPool::shutdown()
{
  bool running;
  mutex_.lock();
  running = running_;
  if (running_) {
    running_ = false;
  }
  mutex_.unlock();
  if (!running) {
    return RPC_ILLEGAL_STATE;
  }
  for (int32_t idx = 0; idx < size_; ++idx) {
    RPCWorker *worker = this->worker(idx);
    if ((worker->flags & THREAD_INITIALIZED) == 0) {
      continue;
    }
    worker->flags &= ~RUNNING;
  }
  asyncQueue_.shutdown();
  for (int32_t idx = 0; idx < size_; ++idx) {
    RPCWorker *worker = this->worker(idx);
    if ((worker->flags & THREAD_INITIALIZED) == 0) {
      continue;
    }
    worker->thread.join();
  }

  return RPC_OK;
}

END_MOCHA_RPC_NAMESPACE
