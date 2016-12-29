#ifndef __MOCA_RPC_SERVER_H__
#define __MOCA_RPC_SERVER_H__ 1
#include <moca/rpc/RPCChannel.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCServerBuilder;
class RPCServerImpl;
class RPCServer;

typedef void (*RPCServerEventListener)(RPCServer *server, RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);

class RPCServer : private RPCNonCopyable
{
private:
  friend class RPCServerBuilder;
  friend class RPCServerImpl;

private:
  RPCServerImpl *impl_;

private:
  explicit RPCServer(RPCServerImpl *impl);
  ~RPCServer();

public:
  class Builder : private RPCNonCopyable
  {
  private:
    friend class RPCServer;
  private:
    RPCServerBuilder *impl_;
  private:
    explicit Builder(RPCServerBuilder *impl);
  public:
    ~Builder();
    Builder *bind(const char *address);
    Builder *bind(const StringLite &address) { return this->bind(address.str()); }
    Builder *timeout(int64_t timeout);
    Builder *headerLimit(int32_t size);
    Builder *payloadLimit(int32_t size);
    Builder *listener(RPCServerEventListener listener, RPCOpaqueData userData, int32_t eventMask = EVENT_TYPE_ALL);
    Builder *keepalive(int64_t interval);
    Builder *id(const char *id);
    Builder *logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData = NULL);
    Builder *id(const StringLite &id) { return this->id(id.str()); }
    Builder *flags(int32_t flags);
    Builder *dispatcher(RPCDispatcher *dispatcher);
    Builder *attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
    Builder *threadPoolSize(int32_t size);
    Builder *connectionLimitPerIp(int32_t limit); /* limit number of connect per second from the same ip address */
    Builder *connectRateLimitPerIp(int32_t limit); /* limit number of connect per second from the same ip address */
    RPCServer *build();
  };

public:
  static Builder *newBuilder();

  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor);
  RPCOpaqueData attachment() const;
  template<typename T>
  T *attachment() const { return static_cast<T *>(attachment()); }

  void addRef();
  bool release();

  void shutdown();

  int32_t submitAsync(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData);
  int32_t submitAsync(RPCChannel *channel, RPCServerEventListener eventListener, RPCOpaqueData eventListenerUserData);
};

END_MOCA_RPC_NAMESPACE

#endif /* __MOCA_RPC_SERVER_H__ */
