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
  RPCMutex asyncMutex_;
  RPCCondVar asyncCond_;
  bool asyncFinished_;
  int32_t asyncStatus_;

public:
  RPCCompletionToken() : asyncFinished_(false), asyncStatus_(0) {
  }
  int32_t init() {
    return 0;
  }
  ~RPCCompletionToken() {
  }

  void finish(int32_t st = 0)
  {
    RPCLock lock(asyncMutex_);
    asyncFinished_ = true;
    asyncStatus_ = st;
    asyncCond_.broadcast();
  }

  void start()
  {
    RPCLock lock(asyncMutex_);
    asyncFinished_ = false;
    asyncStatus_ = 0;
  }

  int32_t wait()
  {
    RPCLock lock(asyncMutex_);
    while (!asyncFinished_) {
      asyncCond_.wait(asyncMutex_);
    }
    return asyncStatus_;
  }
};
END_MOCA_RPC_NAMESPACE
#endif

#endif
