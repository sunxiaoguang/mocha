#ifndef __MOCA_RPC_CLIENT_INTERNAL_H__
#define __MOCA_RPC_CLIENT_INTERNAL_H__

#include "moca/rpc/RPCClient.h"
#include "RPCNano.h"
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCClientImpl
{
private:
  struct EventListenerHandle
  {
    EventListener listener;
    RPCOpaqueData userData;
    int32_t mask;
    int32_t reserved;
    EventListenerHandle(EventListener l = NULL, RPCOpaqueData u = NULL, int32_t m = 0) : listener(l), userData(u), mask(m), reserved(0) { }
  };

  struct BufferSinkArgument
  {
    void *buffer;
    size_t offset;
  };

  template<typename T>
  struct PrimitiveSinkArgument
  {
    T *result;
    size_t offset;
  };

  struct FirePayloadEventSinkArgument
  {
    RPCClientImpl *client;
    bool commit;
    int32_t dispatched;
    int32_t pending;
    int32_t status;
  };

  typedef void (*ReadDataSink)(void *source, size_t size, void *argument);
  typedef int (*GetSocketAddress)(int socket, struct sockaddr *addr, socklen_t *addrLen);

private:
  enum {
    NEGOTIATION_FLAG_ACCEPT_ZLIB = 1,
    PACKET_FLAG_HEADER_ZLIB_ENCODED = 1,
  };
  enum {
    PACKET_TYPE_REQUEST = 0,
    PACKET_TYPE_RESPONSE = 1,
    PACKET_TYPE_HINT = 2,
  };

  enum {
    HINT_CODE_KEEPALIVE = 0,
  };

  enum {
    STATE_NEGOTIATION_MAGIC,
    STATE_NEGOTIATION_FLAGS,
    STATE_NEGOTIATION_PEER_ID_SIZE,
    STATE_NEGOTIATION_PEER_ID,
    STATE_PACKET_ID,
    STATE_PACKET_CODE,
    STATE_PACKET_FLAGS,
    STATE_PACKET_HEADER_SIZE,
    STATE_PACKET_PAYLOAD_SIZE,
    STATE_PACKET_HEADER,
    STATE_PACKET_PAYLOAD,
    STATE_START_OVER,
  };

private:
  RPCClient *wrapper_;
  mutable int32_t fd_;
  mutable int32_t refcount_;
  int64_t timeout_;
  int64_t keepalive_;
  mutable pthread_mutex_t mutex_;
  pthread_t dispatcherThreadId_;
  KeyValuePairs<int32_t, EventListenerHandle> listeners_;
  mutable int32_t pipe_[2];
  pollfd fdset_[2];
  bool running_;
  mutable pthread_mutex_t stateMutex_;
  int32_t state_;
  mutable int64_t currentId_;

  ChainedBuffer *writeBuffer_;
  ChainedBuffer *readBuffer_;
  ChainedBuffer **nextBuffer_;
  size_t readAvailable_;
  size_t readBufferOffset_;
  mutable pthread_mutex_t pendingMutex_;
  mutable ChainedBuffer *pendingBuffer_;
  mutable ChainedBuffer *currentPendingBuffer_;
  mutable ChainedBuffer **nextPendingBuffer_;
  mutable int32_t numPendingBuffers_;

  /* negotiation */
  bool littleEndian_;
  int32_t version_;
  int32_t flags_;
  int32_t peerIdSize_;
  StringLite peerId_;
  StringLite localId_;

  /* request */
  mutable int64_t requestId_;
  int32_t requestCode_;
  int32_t requestFlags_;
  int32_t requestType_;
  int32_t requestHeaderSize_;
  int32_t requestPayloadSize_;
  int32_t dispatchedPayloadSize_;
  KeyValuePairs<StringLite, StringLite> requestHeaders_;

  RPCLogger logger_;
  LogLevel level_;
  RPCOpaqueData loggerUserData_;

  static int32_t emptyData_;

private:
  int32_t sendNegotiation();
  int32_t initMultiThread();
  int32_t initBuffer();
  int32_t initLocalId();

  static void lock(pthread_mutex_t *mutex);
  static void unlock(pthread_mutex_t *mutex);

  void lock() const { lock(&mutex_); }
  void unlock() const { unlock(&mutex_); }

  void lockPending() const { lock(&pendingMutex_); }
  void unlockPending() const { unlock(&pendingMutex_); }

  void lockState() const { lock(&stateMutex_); }
  void unlockState() const { unlock(&stateMutex_); }

  bool releaseLocked() const;
  void addRefLocked() const;
  int64_t nextRequestId() const;

  void setState(int32_t state) { int32_t oldState; lockState(); oldState = state_; state_ = state; unlockState();RPC_LOG_TRACE("Change state from %d to %d", oldState, state);}
  int32_t getState() const;

  bool isRunningLocked() const { return running_; }
  bool isRunning() const;

  void setRunningLocked(bool running) { running_ = running; }
  void setRunning(bool running);

  static void readToBufferSink(void *data, size_t size, void *userData);
  static void readToStringSink(void *data, size_t size, void *userData);
  template<typename T>
  static void readPrimitiveSink(void *data, size_t size, void *userData);
  static void firePayloadEventSink(void *data, size_t size, void *userData);

  int32_t doRead(size_t size, ReadDataSink sink, void *sinkUserData, int32_t nextState);

  int32_t doRead(int32_t *result, int32_t nextState) { PrimitiveSinkArgument<int32_t> argument = {result, 0}; return doRead(sizeof(int32_t), readPrimitiveSink<int32_t>, &argument, nextState); }
  int32_t doRead(int64_t *result, int32_t nextState) { PrimitiveSinkArgument<int64_t> argument = {result, 0}; return doRead(sizeof(int64_t), readPrimitiveSink<int64_t>, &argument, nextState); }
  int32_t doRead(int32_t size, void *buffer, int32_t nextState) { BufferSinkArgument argument = {buffer, 0}; return doRead(size, readToBufferSink, &argument, nextState); }
  int32_t doRead(int32_t size, StringLite *string, int32_t nextState)
  {
    int32_t st = doRead(size + 1, readToStringSink, string, nextState);
    if (st == RPC_OK) {
      string->resize(size);
    }
    return st;
  }
  int32_t doReadNegotiationMagic();
  int32_t doReadNegotiationFlags();
  int32_t doReadNegotiationPeerIdSize() { return doRead(&peerIdSize_, STATE_NEGOTIATION_PEER_ID); }
  int32_t doReadNegotiationPeerId();
  int32_t doReadPacketId() { return doRead(&requestId_, STATE_PACKET_CODE); }
  int32_t doReadPacketCode() { return doRead(&requestCode_, STATE_PACKET_FLAGS); }
  int32_t doReadPacketFlags()
  {
    int32_t status = doRead(&requestFlags_, STATE_PACKET_HEADER_SIZE);

    if (state_ == STATE_PACKET_HEADER_SIZE) {
      requestType_ = requestFlags_ & 0xFF;
      requestFlags_ >>= 8;
    }

    return status;
  }
  int32_t doReadPacketHeaderSize() { return doRead(&requestHeaderSize_, STATE_PACKET_PAYLOAD_SIZE); }
  int32_t doReadPacketPayloadSize();
  int32_t doReadPacketHeader();
  int32_t doReadPacketPayload();
  int32_t doStartOver();
  int32_t doReadPlainPacketHeader();
  int32_t doReadCompressedPacketHeader();

  int32_t doRead();

  int32_t fillBuffer();

  ChainedBuffer *checkAndGetBuffer();

  void dispatchHint(PacketEventData *data);

  int32_t doFireEvent(int32_t eventType, RPCOpaqueData eventData) const;
  int32_t fireConnectedEvent() const { return doFireEvent(EVENT_TYPE_CONNECTED, NULL); }
  int32_t fireEstablishedEvent() const { return doFireEvent(EVENT_TYPE_ESTABLISHED, NULL); }
  int32_t fireDisconnectedEvent() const { return doFireEvent(EVENT_TYPE_DISCONNECTED, NULL); }
  int32_t firePacketEvent();
  int32_t firePayloadEvent(size_t size, const char *payload, bool commit);
  int32_t fireErrorEvent(int32_t status, const char *message) const;
  int32_t doSendPacket(int64_t id, int32_t code, int32_t type, const KeyValuePairs<StringLite, StringLite> *headers,
      const void *payload, size_t payloadSize) const
  {
    if (getState() <= STATE_NEGOTIATION_PEER_ID) {
      return processErrorHook(RPC_ILLEGAL_STATE);
    }
    if (isDispatcherThread()) {
      return doSendPacketDispatcherThread(id, code, type, headers, payload, payloadSize);
    } else {
      return doSendPacketOtherThread(id, code, type, headers, payload, payloadSize);
    }
  }
  int32_t doSendPacketDispatcherThread(int64_t id, int32_t code, int32_t type,
      const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;
  int32_t doSendPacketOtherThread(int64_t id, int32_t code, int32_t type,
      const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;

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

  int32_t serialize(int64_t id, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const StringLite &data, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const KeyValuePairs<StringLite, StringLite> &pairs, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t deserialize(bool swap, void *current, size_t size, KeyValuePairs<StringLite, StringLite> *pairs);
  int32_t deserialize(bool swap, void *current, size_t *available, StringLite *data);

  void transferToPending(const iovec *iov, size_t iovsize) const;

  void notifyDispatcherThread() const;
  int32_t processErrorHook(int32_t status) const;
  int32_t convertError(const char *func, const char *file, uint32_t line) const;
  int32_t doWriteFully(int fd, iovec *iov, size_t *iovsize) const;
  int32_t writeFully(int fd, iovec *iov, size_t *iovsize) const;

  static void checkAndClose(int32_t *fd);

public:
  RPCClientImpl(RPCLogger logger, LogLevel level, RPCOpaqueData loggerUserData);
  ~RPCClientImpl();

  int32_t init(RPCClient *client, int64_t timeout, int64_t keepalive, int32_t flags);
  int32_t connect(const char *address);
  int32_t addListener(EventListener listener, RPCOpaqueData userData, int32_t eventMask);
  int32_t removeListener(EventListener listener);
  int32_t loop(int32_t flags);
  int32_t breakLoop();
  void close() const;
  int32_t keepalive() const
  {
   RPC_LOG_DEBUG("Send keepalive packet on channel");
    return doSendPacket(nextRequestId(), HINT_CODE_KEEPALIVE, PACKET_TYPE_HINT, NULL, NULL, 0);
  }
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
  {
   RPC_LOG_DEBUG("Send response %d to request %ld", code, id);
    return doSendPacket(id, code, PACKET_TYPE_RESPONSE, headers, payload, payloadSize);
  }

  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
  {
    *id = nextRequestId();
   RPC_LOG_DEBUG("Send request %d with code %d", *id, code);
    return doSendPacket(*id, code, PACKET_TYPE_REQUEST, headers, payload, payloadSize);
  }

  int32_t localAddress(StringLite *localAddress, uint16_t *port) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t checkAndSendPendingPackets();
  void addRef() const;
  bool release() const;
};

END_MOCA_RPC_NAMESPACE
#endif
