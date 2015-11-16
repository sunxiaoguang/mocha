#ifndef __MOCA_RPC_INTERNAL_H__
#define __MOCA_RPC_INTERNAL_H__ 1
#include <moca/rpc/RPCDecl.h>

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
#include <atomic>

BEGIN_MOCA_RPC_NAMESPACE
using namespace std;

template<typename T>
class RPCAtomic
{
private:
  mutable atomic<T> number_;

public:
  RPCAtomic() : number_(0) { }
  RPCAtomic(T initValue) : number_(initValue) { }
  T add(T number) const { return number_.fetch_add(number); }
  T subtract(T number) const { return number_.fetch_sub(number); }
  bool compareAndSet(const T &expect, const T &value) const { return number_.compare_exchange_strong(expect, value); }
  T get() const { return number_.load(); }
  void set(const T &val) { number_.store(val); }
};

END_MOCA_RPC_NAMESPACE

#define MOCA_RPC_ATOMIC_DEFINED 1
#endif

#include "RPCLite.h"

#endif
