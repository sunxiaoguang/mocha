#ifndef __MOCA_RPC_PROTOCOL_INTERNAL_H__
#define __MOCA_RPC_PROTOCOL_INTERNAL_H__

#include "moca/rpc/RPCChannel.h"
#include "RPCNano.h"

BEGIN_MOCA_RPC_NAMESPACE

class RPCProtocol : private RPCNonCopyable
{
public:
  enum {
    NEGOTIATION_FLAG_ACCEPT_ZLIB = 1 << 0,
    NEGOTIATION_FLAG_NO_HINT = 1 << 1,
    NEGOTIATION_FLAG_MASK = 0xFFFF,
  };

  typedef void (*WriteDataSink)(ChainedBuffer **buffer, void *argument);
  typedef void (*EventListener)(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  typedef void (*ShutdownChannel)(RPCOpaqueData channel);

private:
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
    RPCProtocol *protocol;
    int32_t dispatched;
    int32_t payloadSize;
    int32_t status;
  };

  typedef void (*ReadDataSink)(void *source, size_t size, void *argument);

private:
  enum {
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

public:
  enum {
    STATE_NEGOTIATION_MAGIC, /* 0 */
    STATE_NEGOTIATION_FLAGS, /* 1 */
    STATE_NEGOTIATION_PEER_ID_SIZE, /* 2 */
    STATE_NEGOTIATION_PEER_ID, /* 3 */
    STATE_PACKET_ID, /* 4 */
    STATE_PACKET_CODE, /* 5 */
    STATE_PACKET_FLAGS, /* 6 */
    STATE_PACKET_HEADER_SIZE, /* 7 */
    STATE_PACKET_PAYLOAD_SIZE, /* 8 */
    STATE_PACKET_HEADER, /* 9 */
    STATE_PACKET_PAYLOAD, /* 10 */
    STATE_START_OVER, /* 11 */
  };

private:
  RPCOpaqueData channel_;
  ShutdownChannel shutdown_;
  mutable RPCAtomic<int64_t> currentId_;
  int32_t state_;
  int32_t channelFlags_;

  ChainedBuffer *writeBuffer_;
  ChainedBuffer *readBuffer_;
  ChainedBuffer **nextBuffer_;
  size_t readAvailable_;
  size_t readBufferOffset_;
  size_t readPending_;

  /* negotiation */
  bool swapByteOrder_;
  int32_t remoteVersion_;
  int32_t remoteFlags_;
  int32_t remoteIdSize_;
  StringLite remoteId_;
  int32_t localVersion_;
  int32_t localFlags_;
  StringLite localId_;

  /* packet */
  mutable int64_t packetId_;
  int32_t packetCode_;
  int32_t packetFlags_;
  int32_t packetType_;
  int32_t packetHeaderSize_;
  int32_t packetPayloadSize_;
  int32_t dispatchedPayloadSize_;
  KeyValuePairs<StringLite, StringLite> packetHeaders_;
  StringLite payloadBuffer_;

  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;

  EventListener listener_;
  RPCOpaqueData listenerUserData_;

  WriteDataSink writeSink_;
  RPCOpaqueData writeSinkUserData_;

  size_t writeOpaqueDataSize_;
  size_t writeBufferAlignment_;

  int32_t limit_;

private:
  int32_t initBuffer();

  void setState(int32_t state) { int32_t oldState; oldState = state_; state_ = state; RPC_LOG_TRACE("Change state from %d to %d", oldState, state);}

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
  int32_t doReadNegotiationPeerIdSize() { return doRead(&remoteIdSize_, STATE_NEGOTIATION_PEER_ID); }
  int32_t doReadNegotiationPeerId();
  int32_t doReadPacketId() { return doRead(&packetId_, STATE_PACKET_CODE); }
  int32_t doReadPacketCode() { return doRead(&packetCode_, STATE_PACKET_FLAGS); }
  int32_t doReadPacketFlags()
  {
    int32_t status = doRead(&packetFlags_, STATE_PACKET_HEADER_SIZE);

    if (state_ == STATE_PACKET_HEADER_SIZE) {
      packetType_ = packetFlags_ & 0xFF;
      packetFlags_ >>= 8;
    }

    return status;
  }
  inline int32_t doReadPacketHeaderSize();
  inline int32_t doReadPacketPayloadSize();
  inline int32_t doReadPacketHeader();
  int32_t doReadPacketPayload();
  int32_t doStartOver();
  int32_t doReadPlainPacketHeader();
  int32_t doReadCompressedPacketHeader();

  ChainedBuffer *checkAndGetBuffer();

  void dispatchHint(PacketEventData *data);

  int32_t doFireEvent(int32_t eventType, RPCOpaqueData eventData) const;
  int32_t fireEstablishedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_ESTABLISHED, NULL); }
  int32_t fireDisconnectedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_DISCONNECTED, NULL); }
  int32_t firePacketEvent();
  int32_t firePayloadEvent(size_t size, const char *payload, bool commit);
  int32_t fireErrorEvent(int32_t status, const char *message) const;
  int32_t doSendPacket(int64_t id, int32_t code, int32_t type, const KeyValuePairs<StringLite, StringLite> *headers,
      const void *payload, size_t payloadSize) const
  {
    if (getState() <= STATE_NEGOTIATION_PEER_ID) {
      return processErrorHook(RPC_ILLEGAL_STATE);
    }
    return doSendPacketUnsafe(id, code, type, headers, payload, payloadSize);
  }
  int32_t doSendPacketUnsafe(int64_t id, int32_t code, int32_t type,
      const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;

  int32_t serialize(int64_t id, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const StringLite &data, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;
  int32_t serialize(const KeyValuePairs<StringLite, StringLite> &pairs, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const;

  bool isFlagsSet(int32_t flags) const {
    return (channelFlags_ & flags) == flags;
  }

  bool isBufferedPayload() const {
    return isFlagsSet(RPCChannel::CHANNEL_FLAG_BUFFERED_PAYLOAD);
  }

  bool isRemoteHintDisabled() const {
    return (remoteFlags_ & NEGOTIATION_FLAG_NO_HINT) != 0;
  }

  bool isRemoteAcceptZlib() const {
    return (remoteFlags_ & NEGOTIATION_FLAG_ACCEPT_ZLIB) != 0;
  }

public:
  RPCProtocol(RPCOpaqueData channel, ShutdownChannel shutdown, EventListener listener, RPCOpaqueData listenerUserData, WriteDataSink writeSink,
      RPCOpaqueData writeSinkUserData, size_t writeOpaqueDataSize, size_t writeBufferAlignment, int32_t state = STATE_NEGOTIATION_MAGIC);
  ~RPCProtocol();

  void onRead(size_t size);

  int32_t init(const StringLite &id, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags);

  int32_t keepalive()
  {
    if (isRemoteHintDisabled()) {
      RPC_LOG_DEBUG("Peer has disabled protocol hint, no keepalive is sent, update write time only");
      return RPC_CANCELED;
    } else {
      RPC_LOG_DEBUG("Send keepalive packet on channel");
      return doSendPacket(nextRequestId(), HINT_CODE_KEEPALIVE, PACKET_TYPE_HINT, NULL, NULL, 0);
    }
  }

  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    RPC_LOG_DEBUG("Send response %d to request %lld", code, static_cast<long long>(id));
    return doSendPacket(id, code, PACKET_TYPE_RESPONSE, headers, payload, payloadSize);
  }

  int32_t request(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize)
  {
    RPC_LOG_DEBUG("Send request %lld with code %d", static_cast<long long>(id), code);
    return doSendPacket(id, code, PACKET_TYPE_REQUEST, headers, payload, payloadSize);
  }

  int64_t nextRequestId() const {
    return currentId_.add(1);
  }

  int32_t sendNegotiation();

  ChainedBuffer *checkAndGetBuffer(size_t *size);

  bool isEstablished() const { return getState() > STATE_NEGOTIATION_PEER_ID; }

  int32_t processErrorHook(int32_t status) const;
  int32_t fireConnectedEvent() const { return doFireEvent(EVENT_TYPE_CHANNEL_CONNECTED, NULL); }

  size_t pendingSize() const { return readPending_; }

  const StringLite &localId() const { return localId_; }
  const StringLite &remoteId() const { return remoteId_; }
  int32_t getState() const { return state_; }
};

END_MOCA_RPC_NAMESPACE
#endif
