#ifndef __MOCHA_RPC_DISPATCHER_INTERNAL_H_listener_
#define __MOCHA_RPC_DISPATCHER_INTERNAL_H__ 1
#include "mocha/rpc/RPCDispatcher.h"
#include "RPCThreadPool.h"
#include "RPC.h"
BEGIN_MOCHA_RPC_NAMESPACE

class RPCDispatcherBuilder : private RPCNonCopyable
{
private:
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
public:
  RPCDispatcherBuilder();
  ~RPCDispatcherBuilder();
  void logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  RPCDispatcher *build();
};

struct RPCDispatcherPoll
{
  enum {
    POLL_FLAG_INITIALIZED = 1 << 0
  };
  volatile int32_t events;
  volatile int32_t flags;
  uv_poll_t handle;
  RPCDispatcher::Pollable pollable;
  RPCDispatcher::PollEventListener listener;
  RPCOpaqueData userData;
  RPCDispatcherImpl *dispatcher;
};

struct RPCDispatcherTimer
{
  int64_t timeout;
  int64_t repeat;
  uv_timer_t handle;
  RPCDispatcher::TimerEventListener listener;
  RPCOpaqueData userData;
  RPCDispatcherImpl *dispatcher;
};

struct RPCDispatcherAsyncTask
{
  RPCDispatcher::AsyncTask task;
  RPCOpaqueData userData;
  RPCDispatcher *dispatcher;
};

class RPCDispatcherImpl : public RPCObject, private RPCNonCopyable
{
private:
  enum {
    EVENT_LOOP_INITIALIZED = 1 << 0,
  };

private:
  RPCDispatcher *wrapper_;
  uv_loop_t eventLoop_;
  RPCMutex mutex_;
  static RPCThreadLocalKey * volatile dispatchingThreadKey_;
  static RPCOnce initTls_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  RPCCompletionToken asyncToken_;
  uint32_t flags_;
  int32_t numCores_;

  uv_async_t async_;
  RPCAsyncQueue asyncQueue_;
  RPCAtomic<bool> stopped_;

private:
  static void onAsyncStop(RPCOpaqueData data);
  void onAsyncStop();

  static void onAsyncTask(uv_async_t *handle);
  void onAsyncTask();
  static void fireAsyncTask(RPCAsyncTask task, RPCOpaqueData taskUserData, RPCOpaqueData sinkUserData);

  static void onPollCollect(uv_handle_t *handle);
  static void onAsyncPollStart(RPCOpaqueData data);
  void onAsyncPollStart(RPCDispatcherPoll *poll);
  static void onAsyncPollDestroy(RPCOpaqueData data);
  void onAsyncPollDestroy(RPCDispatcherPoll *poll);
  static void onPollEvent(uv_poll_t *handle, int32_t status, int32_t events);

  static void onTimerCollect(uv_handle_t *handle);
  static void onAsyncTimerCreate(RPCOpaqueData data);
  void onAsyncTimerCreate(RPCDispatcherTimer *timer);
  static void onAsyncTimerDestroy(RPCOpaqueData data);
  void onAsyncTimerDestroy(RPCDispatcherTimer *timer);
  static void onTimerEvent(uv_timer_t *handle);
  static void initTls();
  static RPCThreadLocalKey *tls();

public:
  RPCDispatcherImpl(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  ~RPCDispatcherImpl();

  int32_t init();

  uv_loop_t *loop() { return &eventLoop_; }
  inline int32_t run(uv_run_mode mode);
  inline int32_t unsafeRun(uv_run_mode mode);
  int32_t stop();

  RPCLogger logger()
  {
    return logger_;
  }
  RPCLogLevel level()
  {
    return level_;
  }
  RPCOpaqueData loggerUserData()
  {
    return loggerUserData_;
  }
  RPCDispatcher *wrap();

  bool isDispatchingThread() const;
  void attachDispatchingThread();

  int32_t submitAsync(RPCAsyncTask task, RPCOpaqueData data);
  int32_t submitAsync(RPCAsyncTask task, RPCOpaqueData data, int32_t key);

  int32_t numberOfCores() const { return numCores_; }

  int32_t createPoll(RPCDispatcherPoll **poll, RPCDispatcher::Pollable pollable, int32_t events, RPCDispatcher::PollEventListener listener, RPCOpaqueData userData);
  int32_t updatePoll(RPCDispatcherPoll *poll, int32_t events);
  int32_t destroyPoll(RPCDispatcherPoll *poll);

  int32_t createTimer(RPCDispatcherTimer **timer, int32_t flags, int64_t timeout, RPCDispatcher::TimerEventListener listener, RPCOpaqueData userData);
  int32_t destroyTimer(RPCDispatcherTimer *timer);
};

class RPCDispatcherThreadImpl : public RPCObject, private RPCNonCopyable
{
private:
  RPCThread thread_;
  RPCDispatcher *dispatcher_;
  RPCDispatcherThread *wrapper_;
  RPCAtomic<bool> running_;

private:
  static RPCOpaqueData threadEntry(RPCOpaqueData argument);

  void threadEntry();

public:
  RPCDispatcherThreadImpl();
  ~RPCDispatcherThreadImpl();

  static RPCDispatcherThread *create(RPCDispatcher *dispatcher);

  int32_t interrupt();
  int32_t join();

  int32_t start(RPCDispatcher *dispatcher);

  RPCDispatcherThread *wrap();
};

END_MOCHA_RPC_NAMESPACE
#endif
