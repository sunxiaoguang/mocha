#ifndef __MOCA_RPC_CHANNEL_INTERNAL_H__
#define __MOCA_RPC_CHANNEL_INTERNAL_H__ 1

#include <moca/rpc/RPCChannel.h>
#include "RPC.h"

#define RPC_INVALID_CHANNEL_ID (0xFFFFFFFFFFFFFFFFL)

BEGIN_MOCA_RPC_NAMESPACE

class RPCServerChannel;
class RPCClientChannel;
typedef uint64_t RPCChannelId;

class RPCChannelImpl : public RPCObject, private RPCNonCopyable
{
protected:
  enum ChannelState {
    UNINITIALIZED,
    INITIALIZING_MULTITHREAD,
    INITIALIZING_RESOLVE,
    INITIALIZING_SOCKET,
    INITIALIZED,
    CLOSING,
    CLOSED,
  };

protected:
  bool timerInitialized_;
  bool keepaliveEnabled_;
  bool timeoutEnabled_;
  uint16_t localPort_;
  uint16_t remotePort_;
  int32_t key_;
  int32_t channelFlags_;
  int32_t limit_;
  int32_t listenerMask_;
  int32_t asyncStatus_;
  RPCChannel *wrapper_;
  mutable uv_loop_t *eventLoop_;
  mutable uv_tcp_t handle_;
  int64_t timeout_; /* public interfaces always use microsecond whereas internally it is millisecond instead */
  int64_t keepaliveInterval_; /* public interfaces always use microsecond whereas internally it is millisecond instead */
  RPCEventListener listener_;
  RPCOpaqueData listenerUserData_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;

  mutable RPCCompletionToken asyncToken_;
  uv_getaddrinfo_t resolver_;

  StringLite localAddress_;
  sockaddr_storage remoteSockAddress_;
  StringLite remoteAddress_;

  StringLite address_;

  StringLite localId_;
  StringLite remoteId_;

  RPCDispatcher *dispatcher_;
  uv_timer_t timer_;

protected:
  int32_t doFireEvent(int32_t eventType, RPCOpaqueData eventData) const;
  int32_t fireCreatedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_CREATED, NULL); }
  int32_t fireConnectedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_CONNECTED, NULL); }
  int32_t fireListeningEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_LISTENING, NULL); }
  int32_t fireDisconnectedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_DISCONNECTED, NULL); }
  int32_t fireErrorEvent(int32_t status, const char *message) const;
  int32_t fireDestroyedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_DESTROYED, NULL); }
  int32_t processErrorHook(int32_t status) const;
  typedef int (*GetSocketAddress)(const uv_tcp_t *socket, struct sockaddr *addr, int32_t *addrLen);
  int32_t getAddress(GetSocketAddress get, StringLite *address, uint16_t *port) const;
  int32_t getAddress(GetSocketAddress get, sockaddr_storage *address, int32_t *size) const;
  void initializedState() { channelState_.compareAndSet(UNINITIALIZED, INITIALIZED); }

  virtual void onClose();

  bool isFlagsSet(int32_t flags) const {
    return (channelFlags_ & flags) == flags;
  }

  bool isBlocking() const {
    return isFlagsSet(RPCChannel::CHANNEL_FLAG_BLOCKING);
  }

  void finishAsync(int32_t st);

  bool isDispatchingThread() const;

  bool isClosing() const { return channelState_.get() >= CLOSING; }
private:
  mutable RPCAtomic<int32_t> channelState_;
  RPCOpaqueData attachment_;
  RPCOpaqueDataDestructor attachmentDestructor_;

private:
  void doClose(bool dispatchingThread);
  int32_t initSocket(int32_t domain);
  virtual int32_t onResolved(struct addrinfo *res) = 0;
  static void onResolved(uv_getaddrinfo_t *resolver, int32_t status, struct addrinfo *res);
  void onResolved(int32_t status, struct addrinfo *res);
  void onResolve();
  static void onAsyncResolve(RPCOpaqueData data);
  static void onClose(uv_handle_t *handle);
  static void onAsyncClose(RPCOpaqueData data);

  void destroyAttachment()
  {
    if (attachment_ && attachmentDestructor_) {
      attachmentDestructor_(attachment_);
    }
  }
  static void onTimer(uv_timer_t *handle);
  virtual void onTimer(uint64_t now);

protected:
  virtual void onAsyncClose();

  bool isKeepaliveEnabled() const { return keepaliveEnabled_; }
  bool isTimeoutEnabled() const { return timeoutEnabled_; }

public:
  RPCChannelImpl();
  virtual ~RPCChannelImpl();
  void close();
  int32_t start();

  RPCChannel *wrap() {
    wrapper_ = new RPCChannel(this);
    return wrapper_;
  }

  int32_t init(int32_t flags, RPCDispatcher *dispatcher, const StringLite &id, const StringLite &address, RPCEventListener listener,
      RPCOpaqueData listenerOpaqueData, int32_t listenerMask, int64_t timeout, int32_t limit, int64_t keepaliveInterval,
      RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);

  void inherit(RPCChannelImpl *client);

  uv_loop_t *loop() const { return eventLoop_; }
  uv_tcp_t *handle() const { return &handle_; }

  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const
  {
    if (localAddress) {
      *localAddress = localAddress_;
    }
    if (port) {
      *port = localPort_;
    }
    return RPC_OK;
  }

  void remoteAddress(sockaddr_storage *address) const
  {
    *address = remoteSockAddress_;
  }

  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const
  {
    if (remoteAddress) {
      *remoteAddress = remoteAddress_;
    }
    if (port) {
      *port = remotePort_;
    }
    return RPC_OK;
  }
  int32_t localId(StringLite *localId) const
  {
    *localId = localId_;
    return RPC_OK;
  }
  int32_t remoteId(StringLite *remoteId) const
  {
    *remoteId = remoteId_;
    return RPC_OK;
  }

  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor)
  {
    destroyAttachment();
    attachment_ = attachment;
    attachmentDestructor_ = destructor;
  }

  RPCOpaqueData attachment() const
  {
    return attachment_;
  }

  int32_t key() const
  {
    return key_;
  }

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  int32_t localAddress(string *localAddress, uint16_t *port = NULL) const
  {
    StringLite tmp;
    int32_t st = this->localAddress(&tmp, port);
    localAddress->assign(tmp.str(), tmp.size());
    return st;
  }
  int32_t remoteAddress(string *remoteAddress, uint16_t *port = NULL) const
  {
    StringLite tmp;
    int32_t st = this->remoteAddress(&tmp, port);
    remoteAddress->assign(tmp.str(), tmp.size());
    return st;
  }
  int32_t localId(string *localId) const
  {
    StringLite tmp;
    int32_t st = this->localId(&tmp);
    localId->assign(tmp.str(), tmp.size());
    return st;
  }
  int32_t remoteId(string *remoteId) const
  {
    StringLite tmp;
    int32_t st = this->remoteId(&tmp);
    remoteId->assign(tmp.str(), tmp.size());
    return st;
  }
  int32_t response(int64_t id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
#endif
};

class RPCChannelBuilder : private RPCNonCopyable
{
private:
  bool isClient_;
  StringLite address_;
  RPCEventListener listener_;
  RPCOpaqueData listenerUserData_;
  int32_t listenerMask_;
  int32_t limit_;
  int32_t flags_;
  int64_t timeout_;
  int64_t keepaliveInterval_;
  StringLite id_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  RPCDispatcher *dispatcher_;
  RPCOpaqueData attachment_;
  RPCOpaqueDataDestructor attachmentDestructor_;

private:
  void address(const char *address, bool isClient);
  void destroyAttachment();

public:
  RPCChannelBuilder();
  ~RPCChannelBuilder();
  void bind(const char *address);
  void connect(const char *address);
  void timeout(int64_t timeout);
  void limit(int32_t size);
  void listener(RPCEventListener listener, RPCOpaqueData userData, int32_t eventMask);
  void keepalive(int64_t interval);
  void id(const char *id);
  void logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  void dispatcher(RPCDispatcher *dispatcher);
  void flags(int32_t flags);
  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
  RPCChannel *build();
};

END_MOCA_RPC_NAMESPACE
#endif
