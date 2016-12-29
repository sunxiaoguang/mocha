#ifndef __MOCA_RPC_INTERNAL_H__
#define __MOCA_RPC_INTERNAL_H__ 1
#include <moca/rpc/RPCDecl.h>

#include "RPCLite.h"
#if !defined(MOCA_RPC_NANO)
#include <uv.h>
BEGIN_MOCA_RPC_NAMESPACE
class RPCCompletionToken
{
private:
  uv_mutex_t asyncMutex_;
  uv_cond_t asyncCond_;
  bool asyncFinished_;
  int32_t initialized_;
  int32_t asyncStatus_;

public:
  RPCCompletionToken() : asyncFinished_(false), initialized_(0), asyncStatus_(0) {
    memset(&asyncMutex_, 0, sizeof(asyncMutex_));
    memset(&asyncCond_, 0, sizeof(asyncCond_));
  }
  int32_t init() {
    int32_t st;
    if ((st = uv_mutex_init(&asyncMutex_))) {
      return st;
    }
    ++initialized_;
    if ((st = uv_cond_init(&asyncCond_))) {
      return st;
    }
    ++initialized_;
    return 0;
  }
  ~RPCCompletionToken() {
    if (initialized_) {
      uv_mutex_destroy(&asyncMutex_);
      if (--initialized_) {
        uv_cond_destroy(&asyncCond_);
      }
    }
  }

  void finish(int32_t st = 0)
  {
    uv_mutex_lock(&asyncMutex_);
    asyncFinished_ = true;
    asyncStatus_ = st;
    uv_cond_broadcast(&asyncCond_);
    uv_mutex_unlock(&asyncMutex_);
  }

  void start()
  {
    ScopedMutex mutex(&asyncMutex_);
    asyncFinished_ = false;
    asyncStatus_ = 0;
  }

  int32_t wait()
  {
    ScopedMutex mutex(&asyncMutex_);
    while (!asyncFinished_) {
      uv_cond_wait(&asyncCond_, &asyncMutex_);
    }
    return asyncStatus_;
  }
};
END_MOCA_RPC_NAMESPACE
#endif

#endif
