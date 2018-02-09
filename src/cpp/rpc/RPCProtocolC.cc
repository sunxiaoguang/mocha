#include "RPCProtocolC.h"
#include "RPCProtocol.h"
#include "RPCLite.h"

using namespace mocha::rpc;

struct MochaRPCProtocol : MochaRPCWrapper
{
  RPCProtocol protocol;
  MochaRPCProtocolShutdown shutdown;
  MochaRPCProtocolListener listener;
  MochaRPCProtocolWrite write;
  MochaRPCOpaqueData userData;
  MochaRPCPacketEventData packet;
  MochaRPCProtocol(MochaRPCProtocolShutdown shutdown, MochaRPCProtocolListener listener, MochaRPCProtocolWrite write, MochaRPCOpaqueData userData, int32_t state);
  ~MochaRPCProtocol();
  static void writeDataSink(ChainedBuffer **buffer, void *argument);
  static void eventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  static void shutdownChannel(RPCOpaqueData channel);
};

void
MochaRPCProtocol::writeDataSink(ChainedBuffer **buffer, void *argument)
{
  MochaRPCProtocol *protocol = static_cast<MochaRPCProtocol *>(argument);
  ChainedBuffer *head = *buffer;

  while (head) {
    protocol->write(head->get(), head->getSize(), protocol->userData);
    head = head->next;
  }

  ChainedBuffer::destroy(buffer);
}

void
MochaRPCProtocol::eventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MochaRPCProtocol *protocol = static_cast<MochaRPCProtocol *>(userData);
  switch (eventType) {
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
    case MOCHA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
      convert(static_cast<PacketEventData *>(eventData)->headers, protocol);
      static_cast<PacketEventData *>(eventData)->headers = reinterpret_cast<const KeyValuePairs<StringLite, StringLite> *>(protocol->headers);
      break;
    default:
      break;
  }
  protocol->listener(eventType, eventData, protocol->userData);
}

void
MochaRPCProtocol::shutdownChannel(RPCOpaqueData argument)
{
  MochaRPCProtocol *protocol = static_cast<MochaRPCProtocol *>(argument);
  protocol->shutdown(protocol->userData);
}

MochaRPCOpaqueData MochaRPCProtocolUserData(MochaRPCProtocol *protocol)
{
  return protocol->userData;
}

MochaRPCProtocol::MochaRPCProtocol(MochaRPCProtocolShutdown shutdown1, MochaRPCProtocolListener listener1, MochaRPCProtocolWrite write1, MochaRPCOpaqueData userData1, int32_t state)
  : protocol(this, shutdownChannel, eventListener, this, writeDataSink, this, 0, 8, state), shutdown(shutdown1), listener(listener1), write(write1), userData(userData1)
{
  refcount = 1;
  headers = NULL;
  buffer = NULL;
  attachment = NULL;
  attachmentDestructor = NULL;
  memset(&empty, 0, sizeof(empty));
}

MochaRPCProtocol::~MochaRPCProtocol()
{
}

MochaRPCProtocol *MochaRPCProtocolCreate(MochaRPCProtocolShutdown shutdown, MochaRPCProtocolListener listener, MochaRPCProtocolWrite write, MochaRPCOpaqueData userData)
{
  return new MochaRPCProtocol(shutdown, listener, write, userData, RPCProtocol::STATE_NEGOTIATION_MAGIC);
}

MochaRPCProtocol *MochaRPCProtocolCreateNegotiated(MochaRPCProtocolShutdown shutdown, MochaRPCProtocolListener listener, MochaRPCProtocolWrite write, MochaRPCOpaqueData userData)
{
  return new MochaRPCProtocol(shutdown, listener, write, userData, RPCProtocol::STATE_PACKET_ID);
}

int32_t MochaRPCProtocolRead(MochaRPCProtocol *protocol, MochaRPCOpaqueData data, int32_t size)
{
  char *realData = static_cast<char *>(data);
  size_t realSize = size, total = size;
  while (size > 0) {
    ChainedBuffer *buffer = protocol->protocol.checkAndGetBuffer(&realSize);
    memcpy(buffer->allocate(realSize), realData, realSize);
    size -= static_cast<int32_t>(realSize);
    realData += realSize;
  }
  protocol->protocol.onRead(total);
  return MOCHA_RPC_OK;
}

int32_t MochaRPCProtocolStart(MochaRPCProtocol *protocol, const char *id, MochaRPCLogger logger, MochaRPCLogLevel level, MochaRPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags)
{
  StringLite uuid;
  if (id == NULL) {
    uuidGenerate(&uuid);
    id = uuid.str();
  }
  return protocol->protocol.init(id, reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), loggerUserData, flags, limit, channelFlags);
}

int32_t MochaRPCProtocolKeepAlive(MochaRPCProtocol *protocol)
{
  return protocol->protocol.keepalive();
}

int32_t MochaRPCProtocolResponse(MochaRPCProtocol *protocol, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return protocol->protocol.response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return protocol->protocol.response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t MochaRPCProtocolRequest(MochaRPCProtocol *protocol, int64_t *id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  return MochaRPCProtocolRequest3(protocol, (*id = protocol->protocol.nextRequestId()), code, headers, payload, payloadSize);
}

int32_t MochaRPCProtocolRequest2(MochaRPCProtocol *protocol, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  int64_t ignored;
  return MochaRPCProtocolRequest(protocol, &ignored, code, headers, payload, payloadSize);
}

int32_t MochaRPCProtocolRequest3(MochaRPCProtocol *protocol, int64_t id, int32_t code, const MochaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return protocol->protocol.request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return protocol->protocol.request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t MochaRPCProtocolSendNegotiation(MochaRPCProtocol *protocol)
{
  return protocol->protocol.sendNegotiation();
}

int32_t MochaRPCProtocolIsEstablished(MochaRPCProtocol *protocol)
{
  return protocol->protocol.isEstablished();
}

int32_t MochaRPCProtocolProcessError(MochaRPCProtocol *protocol, int32_t status)
{
  return protocol->protocol.processErrorHook(status);
}

int32_t MochaRPCProtocolConnected(MochaRPCProtocol *protocol)
{
  return protocol->protocol.fireConnectedEvent();
}

const char *MochaRPCProtocolLocalId(MochaRPCProtocol *protocol)
{
  return protocol->protocol.localId().str();
}

const char *MochaRPCProtocolRemoteId(MochaRPCProtocol *protocol)
{
  return protocol->protocol.remoteId().str();
}

int32_t MochaRPCProtocolPendingSize(MochaRPCProtocol *protocol)
{
  return protocol->protocol.pendingSize();
}

void MochaRPCProtocolDestroy(MochaRPCProtocol *protocol)
{
  delete protocol;
}
