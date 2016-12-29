#ifndef __MOCA_RPC_DISPATCHER_H__
#define __MOCA_RPC_DISPATCHER_H__ 1
#include <moca/rpc/RPC.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCDispatcher;
class RPCDispatcherImpl;
class RPCDispatcherThread;
class RPCDispatcherThreadImpl;
class RPCDispatcherBuilder;

class RPCPoll : private RPCNonCopyable
{
private:
  RPCPoll();
  ~RPCPoll();
};

class RPCDispatcher : private RPCNonCopyable
{
private:
  friend class RPCDispatcherImpl;
  friend class RPCDispatcherBuilder;

private:
  RPCDispatcherImpl *impl_;

private:
  explicit RPCDispatcher(RPCDispatcherImpl *impl);
  ~RPCDispatcher();

public:
  enum RunFlags {
    RUN_FLAG_DEFAULT = 0,
    RUN_FLAG_ONE_SHOT = 1,
    RUN_FLAG_NONBLOCK = 2,
  };
  enum PollEvents {
    POLL_READABLE = 1 << 0,
    POLL_WRITABLE = 1 << 1,
    POLL_READWRITE = POLL_READABLE | POLL_WRITABLE,
    POLL_DISCONNECT = 1 << 2,
    POLL_DESTROYED = 1 << 30,
  };
  enum TimerFlags {
    TIMER_FLAG_ONE_SHOT = 0,
    TIMER_FLAG_REPEAT = 1 << 0,
  };

  typedef struct Poll Poll;
  typedef int32_t Pollable;
  typedef void (*PollEventListener)(Poll *poll, Pollable pollable, int32_t status, int32_t events, RPCOpaqueData userData);

  typedef struct Timer Timer;
  typedef void (*TimerEventListener)(Timer *timer, RPCOpaqueData userData);

public:
  class Builder : private RPCNonCopyable
  {
  private:
    friend class RPCDispatcher;
  private:
    RPCDispatcherBuilder *impl_;
  private:
    explicit Builder(RPCDispatcherBuilder *impl);
  public:
    ~Builder();
    Builder *logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData = NULL);
    RPCDispatcher *build();
  };

  static Builder *newBuilder();

  int32_t stop();
  int32_t run(int32_t flags = RUN_FLAG_DEFAULT);

  void addRef();
  bool release();

  int32_t createPoll(Poll **poll, Pollable pollable, int32_t events, PollEventListener listener, RPCOpaqueData userData = NULL);
  int32_t updatePoll(Poll *poll, int32_t events);
  int32_t destroyPoll(Poll *poll);

  int32_t createTimer(Timer **timer, int32_t flags, int64_t timeout, TimerEventListener listener, RPCOpaqueData userData = NULL);
  int32_t destroyTimer(Timer *timer);

  bool isDispatchingThread() const;
};

class RPCDispatcherThread : private RPCNonCopyable
{
private:
  friend class RPCDispatcherThreadImpl;

private:
  RPCDispatcherThreadImpl *impl_;
private:
  explicit RPCDispatcherThread(RPCDispatcherThreadImpl *impl);
  ~RPCDispatcherThread();

public:

  static RPCDispatcherThread *create(RPCDispatcher *dispatcher);

  int32_t shutdown();
  int32_t interrupt();
  int32_t join();

  void addRef();
  bool release();
};

END_MOCA_RPC_NAMESPACE

#endif
