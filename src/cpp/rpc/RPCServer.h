#ifndef __MOCA_RPC_SERVER_INTERNAL_H__
#define __MOCA_RPC_SERVER_INTERNAL_H__ 1

#include <moca/rpc/RPCServer.h>
#include "RPCThreadPool.h"
#include <sys/socket.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCServerImpl : public RPCObject, private RPCNonCopyable
{
private:
  struct AsyncTaskContext {
    RPCServerImpl *server;
    RPCChannel *channel;
    int32_t eventType;
    RPCOpaqueData eventData;
    RPCServerEventListener eventListener;
    RPCOpaqueData eventListenerUserData;
  };

  struct AddressHash {
    uint32_t operator()(const sockaddr_storage &lhs) const;
  };

  struct AddressEquals {
    bool operator()(const sockaddr_storage &lhs, const sockaddr_storage &rhs) const;
  };

  struct AddressStatistics {
    int32_t connections;
    int32_t connects;
    bool busted;
    time_t minute;
  };

  enum {
    MUTEX_INITIALIZED = 1 << 0,
    COND_INITIALIZED = 1 << 1,
    TIMER_INITIALIZED = 1 << 2,
  };

private:
  static void listener(RPCChannel *channel, int32_t eventType,
      RPCOpaqueData eventData, RPCOpaqueData userData);

  void onEvent(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData);

  AsyncTaskContext *duplicatePacketEventData(RPCOpaqueData eventData);
  AsyncTaskContext *duplicatePayloadEventData(RPCOpaqueData eventData);
  AsyncTaskContext *duplicateErrorEventData(RPCOpaqueData eventData);

  void onAsyncTask(AsyncTaskContext *context);
  static void asyncTaskEntry(RPCOpaqueData userData);

  AddressStatistics *checkAndGetStat(const sockaddr_storage &addr);
  bool connect(RPCChannel *channel);
  void disconnect(RPCChannel *channel);

  static void onTimer(uv_timer_t *handle);
  void onTimer();
  static void visitCleanupCandidate(const sockaddr_storage &key, const int32_t &value, RPCOpaqueData userData);
  void visitCleanupCandidate(const sockaddr_storage &addr);
  static void onAsyncShutdown(RPCOpaqueData userData);
  void onAsyncShutdown();
  static void onAsyncShutdownDone(uv_handle_t *handle);

private:
  RPCServer *wrapper_;
  RPCChannel *channel_;
  bool channelDestroyed_;
  bool createEventDispatched_;
  int32_t initializedFlags_;

  uv_mutex_t mutex_;
  uv_cond_t cond_;

  RPCServerEventListener listener_;
  RPCOpaqueData listenerUserData_;
  int32_t listenerEventMask_;
  int32_t payloadLimit_;
  RPCDispatcher *dispatcher_;

  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;

  RPCThreadPool *threadPool_;
  int32_t connectionLimitPerIp_;
  int32_t connectRateLimitPerIp_;
  RPCHashMap<sockaddr_storage, AddressStatistics, AddressHash, AddressEquals> statistics_;
  RPCHashMap<sockaddr_storage, int32_t, AddressHash, AddressEquals> cleanupCandidates_;
  uv_timer_t timer_;
  RPCAtomic<bool> stopping_;

public:
  RPCServerImpl();
  ~RPCServerImpl();

  int32_t init(RPCDispatcher *dispatcher, RPCChannel::Builder *builder, RPCServerEventListener listener,
               RPCOpaqueData listenerUserData, int32_t listenerEventMask, int32_t payloadLimit,
               RPCLogger logger, RPCLogLevel loggerLevel, RPCOpaqueData loggerUserData,
               int32_t threadPoolSize, int32_t connectionLimitPerIp, int32_t connectRateLimitPerIp);

  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor);
  RPCOpaqueData attachment() const;

  void shutdown();
  RPCServer *wrap()
  {
    wrapper_ = new RPCServer(this);
    return wrapper_;
  }

  int32_t submitAsync(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData);
  int32_t submitAsync(RPCChannel *channel, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData);
};

class RPCServerBuilder : private RPCNonCopyable
{
private:
  RPCChannel::Builder *builder_;
  RPCServerEventListener listener_;
  RPCOpaqueData listenerUserData_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  int32_t listenerEventMask_;
  int32_t payloadLimit_;
  int32_t threadPoolSize_;
  RPCDispatcher *dispatcher_;
  int32_t connectionLimitPerIp_;
  int32_t connectRateLimitPerIp_;

public:
  RPCServerBuilder();
  ~RPCServerBuilder();
  void bind(const char *address);
  void timeout(int64_t timeout);
  void headerLimit(int32_t size);
  void payloadLimit(int32_t size);
  void listener(RPCServerEventListener listener, RPCOpaqueData userData, int32_t eventMask);
  void keepalive(int64_t interval);
  void id(const char *id);
  void logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  void dispatcher(RPCDispatcher *dispatcher);
  void flags(int32_t flags);
  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
  void threadPoolSize(int32_t size);
  void connectionLimitPerIp(int32_t limit);
  void connectRateLimitPerIp(int32_t limit);
  RPCServer *build();
};

END_MOCA_RPC_NAMESPACE

#endif /* __MOCA_RPC_SERVER_INTERNAL_H__ */
