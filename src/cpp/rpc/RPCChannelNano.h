#ifndef __MOCA_RPC_NANO_CHANNEL_INTERNAL_H__
#define __MOCA_RPC_NANO_CHANNEL_INTERNAL_H__

#include "moca/rpc/RPCChannelNano.h"
#include "RPCProtocol.h"
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCChannelNanoImpl : public RPCObject, private RPCNonCopyable
{
private:
  typedef int (*GetSocketAddress)(int socket, struct sockaddr *addr, socklen_t *addrLen);
private:
  RPCProtocol protocol_;
  RPCChannelNano *wrapper_;
  mutable int32_t fd_;
  int64_t timeout_;
  int64_t keepalive_;
  mutable pthread_mutex_t mutex_;
  pthread_t dispatcherThreadId_;
  RPCChannelNano::EventListener listener_;
  int32_t listenerMask_;
  RPCOpaqueData listenerUserData_;
  mutable int32_t pipe_[2];
  mutable pollfd fdset_[2];
  mutable bool running_;
  mutable int64_t currentId_;

  mutable pthread_mutex_t pendingMutex_;
  mutable ChainedBuffer *pendingBuffer_;
  mutable ChainedBuffer *currentPendingBuffer_;
  mutable ChainedBuffer **nextPendingBuffer_;
  mutable int32_t numPendingBuffers_;

  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;

  static int32_t emptyData_;
  RPCOpaqueData attachment_;
  RPCOpaqueDataDestructor attachmentDestructor_;

private:
  int32_t sendNegotiation();
  int32_t initMultiThread();
  int32_t initLocalId();

  static void lock(pthread_mutex_t *mutex);
  static void unlock(pthread_mutex_t *mutex);

  void lock() const { lock(&mutex_); }
  void unlock() const { unlock(&mutex_); }

  void lockPending() const { lock(&pendingMutex_); }
  void unlockPending() const { unlock(&pendingMutex_); }

  bool isRunningLocked() const { return running_; }
  bool isRunning() const;

  void setRunningLocked(bool running) { running_ = running; }
  void setRunning(bool running);

  int32_t doRead();

  ChainedBuffer *checkAndGetBuffer();

  int32_t doFireEvent(int32_t eventType, RPCOpaqueData eventData) const;

  int32_t getAddress(GetSocketAddress get, StringLite *localAddress, uint16_t *port) const;

  void clearDispatcherThreadId()
  {
    memset(&dispatcherThreadId_, 0, sizeof(dispatcherThreadId_));
  }

  void updateDispatcherThreadId()
  {
    dispatcherThreadId_ = pthread_self();
  }

  bool isDispatcherThread() const
  {
    lock();
    bool result = pthread_equal(pthread_self(), dispatcherThreadId_);
    unlock();
    return result;
  }
  bool isDispatcherThreadLocked() const
  {
    return pthread_equal(pthread_self(), dispatcherThreadId_);
  }

  void notifyDispatcherThread() const;
  int32_t convertError(const char *func, const char *file, uint32_t line) const;
  int32_t doWriteFully(int fd, iovec *iov, size_t *iovsize) const;
  int32_t writeFully(int fd, iovec *iov, size_t *iovsize) const;

  void doWrite(ChainedBuffer **buffer);

  static void checkAndClose(int32_t *fd);

  static void protocolEventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  static void protocolWriteSink(ChainedBuffer **buffer, RPCOpaqueData argument);
  void destroyAttachment()
  {
    if (attachment_ && attachmentDestructor_) {
      attachmentDestructor_(attachment_);
    }
  }
  static void closeChannel(RPCOpaqueData opaque) {
    RPCChannelNanoImpl *channel = static_cast<RPCChannelNanoImpl *>(opaque);
    channel->close();
    channel->doFireEvent(EVENT_TYPE_CHANNEL_DISCONNECTED, NULL);
  }

public:
  enum {
    NEGOTIATION_FLAG_ACCEPT_ZLIB = 1 << 0,
    NEGOTIATION_FLAG_NO_HINT = 1 << 23,
  };

public:
  RPCChannelNanoImpl(RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData,
      RPCChannelNano::EventListener listener, RPCOpaqueData listenerUserData, int32_t eventMask,
      RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
  virtual ~RPCChannelNanoImpl();

  int32_t init(const StringLite &id, int64_t timeout, int64_t keepalive, int32_t flags, int32_t limit);
  int32_t connect(const char *address);
  int32_t loop(int32_t flags);
  int32_t breakLoop() const;
  void close() const;
  int32_t keepalive()
  {
    return protocol_.keepalive();
  }
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    return protocol_.response(id, code, headers, payload, payloadSize);
  }

  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    return protocol_.request(id, code, headers, payload, payloadSize);
  }

  int32_t localAddress(StringLite *localAddress, uint16_t *port) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t checkAndSendPendingPackets();

  static RPCChannelNano *create(const StringLite &address, const StringLite &id, int64_t timeout, int64_t keepalive, int32_t flags,
      int32_t limit, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, RPCChannelNano::EventListener listener,
      RPCOpaqueData userData, int32_t eventMask, RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);

  RPCChannelNano *wrap() {
    wrapper_ = new RPCChannelNano(this);
    return wrapper_;
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
};

class RPCChannelNanoBuilder : private RPCNonCopyable
{
private:
  StringLite address_;
  RPCChannelNano::EventListener listener_;
  RPCOpaqueData listenerUserData_;
  int32_t listenerMask_;
  int64_t timeout_;
  int32_t limit_;
  int64_t keepaliveInterval_;
  int32_t flags_;
  StringLite id_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  RPCOpaqueData attachment_;
  RPCOpaqueDataDestructor attachmentDestructor_;

private:
  void address(const char *address);
  void destroyAttachment();

public:
  RPCChannelNanoBuilder();
  ~RPCChannelNanoBuilder();
  void connect(const char *address);
  void timeout(int64_t timeout);
  void limit(int32_t size);
  void listener(RPCChannelNano::EventListener listener, RPCOpaqueData userData, int32_t eventMask);
  void keepalive(int64_t interval);
  void id(const char *id);
  void logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  void flags(int32_t flags);
  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
  RPCChannelNano *build();
};

END_MOCA_RPC_NAMESPACE
#endif
