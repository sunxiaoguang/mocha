#ifndef __MOCA_RPC_ATOMIC_INTERNAL_H__
#define __MOCA_RPC_ATOMIC_INTERNAL_H__ 1
#include "moca/rpc/RPC.h"
#include "RPCNano.h"

BEGIN_MOCA_RPC_NAMESPACE

#ifndef MOCA_RPC_FULL
template<typename T>
class RPCAtomic : private RPCNonCopyable
{
private:
  mutable T number_;
  mutable RPCMutex mutex_;

private:
  void lock() const { mutex_.lock(); }
  void unlock() const { mutex_.unlock(); }
  T addLocked(T number) const { T old = number_; number_ += number; return old; }
  T subtractLocked(T number) const { T old = number_; number_ -= number; return old; }

public:
  RPCAtomic() : number_(0) { }
  RPCAtomic(T initValue) : number_(initValue) { }
  T add(T number) const { lock(); T result = addLocked(number); unlock(); return result; }
  T subtract(T number) const { lock(); T result = subtractLocked(number); unlock(); return result; }
  bool compareAndSet(T &expect, const T &value) const { lock(); bool result; if (number_ == expect) { number_ = value; result = true; } else { expect = number_; result = false; } unlock(); return result; }
  bool compareAndSet(const T &expect, const T &value) const { T copy = expect; return compareAndSet(copy, value); }
  T get() const { lock(); T result = number_; unlock(); return result; }
  void set(const T &val) { lock() ; number_ = val; unlock(); }
  ~RPCAtomic() { }
};
#else
#include <atomic>
using namespace std;
template<typename T>
class RPCAtomic : private RPCNonCopyable
{
private:
  mutable atomic<T> number_;

public:
  RPCAtomic() : number_(0) { }
  RPCAtomic(T initValue) : number_(initValue) { }
  T add(T number) const { return number_.fetch_add(number); }
  T subtract(T number) const { return number_.fetch_sub(number); }
  bool compareAndSet(T &expect, const T &value) const { return number_.compare_exchange_strong(expect, value); }
  bool compareAndSet(const T &expect, const T &value) const { T copy = expect; return compareAndSet(copy, value); }
  T get() const { return number_.load(); }
  void set(const T &val) { number_.store(val); }
};
#endif

END_MOCA_RPC_NAMESPACE

#endif /* __MOCA_RPC_ATOMIC_INTERNAL_H__ */
