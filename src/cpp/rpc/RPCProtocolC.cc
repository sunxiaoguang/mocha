#include "RPCProtocolC.h"
#include "RPCProtocol.h"
#include "RPCLite.h"

using namespace moca::rpc;

struct MocaRPCProtocol : MocaRPCWrapper
{
  RPCProtocol protocol;
  MocaRPCProtocolShutdown shutdown;
  MocaRPCProtocolListener listener;
  MocaRPCProtocolWrite write;
  MocaRPCOpaqueData userData;
  MocaRPCPacketEventData packet;
  MocaRPCProtocol(MocaRPCProtocolShutdown shutdown, MocaRPCProtocolListener listener, MocaRPCProtocolWrite write, MocaRPCOpaqueData userData);
  ~MocaRPCProtocol();
  static void writeDataSink(ChainedBuffer **buffer, void *argument);
  static void eventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  static void shutdownChannel(RPCOpaqueData channel);
};

void
MocaRPCProtocol::writeDataSink(ChainedBuffer **buffer, void *argument)
{
  MocaRPCProtocol *protocol = static_cast<MocaRPCProtocol *>(argument);
  ChainedBuffer *head = *buffer;

  while (head) {
    protocol->write(head->get(), head->getSize(), protocol->userData);
    head = head->next;
  }

  ChainedBuffer::destroy(buffer);
}

void
MocaRPCProtocol::eventListener(int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  MocaRPCProtocol *protocol = static_cast<MocaRPCProtocol *>(userData);
  switch (eventType) {
    case MOCA_RPC_EVENT_TYPE_CHANNEL_REQUEST:
    case MOCA_RPC_EVENT_TYPE_CHANNEL_RESPONSE:
      convert(static_cast<PacketEventData *>(eventData)->headers, protocol);
      static_cast<PacketEventData *>(eventData)->headers = reinterpret_cast<const KeyValuePairs<StringLite, StringLite> *>(protocol->headers);
      break;
    default:
      break;
  }
  protocol->listener(eventType, eventData, protocol->userData);
}

void
MocaRPCProtocol::shutdownChannel(RPCOpaqueData argument)
{
  MocaRPCProtocol *protocol = static_cast<MocaRPCProtocol *>(argument);
  protocol->shutdown(protocol->userData);
}

MocaRPCOpaqueData MocaRPCProtocolUserData(MocaRPCProtocol *protocol)
{
  return protocol->userData;
}

MocaRPCProtocol::MocaRPCProtocol(MocaRPCProtocolShutdown shutdown1, MocaRPCProtocolListener listener1, MocaRPCProtocolWrite write1, MocaRPCOpaqueData userData1)
  : protocol(this, shutdownChannel, eventListener, this, writeDataSink, this, 0, 8), shutdown(shutdown1), listener(listener1), write(write1), userData(userData1)
{
  refcount = 1;
  headers = NULL;
  buffer = NULL;
  attachment = NULL;
  attachmentDestructor = NULL;
  memset(&empty, 0, sizeof(empty));
}

MocaRPCProtocol::~MocaRPCProtocol()
{
}

MocaRPCProtocol *MocaRPCProtocolCreate(MocaRPCProtocolShutdown shutdown, MocaRPCProtocolListener listener, MocaRPCProtocolWrite write, MocaRPCOpaqueData userData)
{
  return new MocaRPCProtocol(shutdown, listener, write, userData);
}

int32_t MocaRPCProtocolRead(MocaRPCProtocol *protocol, MocaRPCOpaqueData data, int32_t size)
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
  return MOCA_RPC_OK;
}

int32_t MocaRPCProtocolStart(MocaRPCProtocol *protocol, const char *id, MocaRPCLogger logger, MocaRPCLogLevel level, MocaRPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags)
{
  StringLite uuid;
  if (id == NULL) {
    uuidGenerate(&uuid);
    id = uuid.str();
  }
  return protocol->protocol.init(id, reinterpret_cast<RPCLogger>(logger), static_cast<RPCLogLevel>(level), loggerUserData, flags, limit, channelFlags);
}

int32_t MocaRPCProtocolKeepAlive(MocaRPCProtocol *protocol)
{
  return protocol->protocol.keepalive();
}

int32_t MocaRPCProtocolResponse(MocaRPCProtocol *protocol, int64_t id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return protocol->protocol.response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return protocol->protocol.response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t MocaRPCProtocolRequest(MocaRPCProtocol *protocol, int64_t *id, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return protocol->protocol.request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return protocol->protocol.request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t MocaRPCProtocolRequest2(MocaRPCProtocol *protocol, int32_t code, const MocaRPCKeyValuePairs *headers, const void *payload, size_t payloadSize)
{
  int64_t ignored;
  return MocaRPCProtocolRequest(protocol, &ignored, code, headers, payload, payloadSize);
}

int32_t MocaRPCProtocolSendNegotiation(MocaRPCProtocol *protocol)
{
  return protocol->protocol.sendNegotiation();
}

int32_t MocaRPCProtocolIsEstablished(MocaRPCProtocol *protocol)
{
  return protocol->protocol.isEstablished();
}

int32_t MocaRPCProtocolProcessError(MocaRPCProtocol *protocol, int32_t status)
{
  return protocol->protocol.processErrorHook(status);
}

int32_t MocaRPCProtocolConnected(MocaRPCProtocol *protocol)
{
  return protocol->protocol.fireConnectedEvent();
}

const char *MocaRPCProtocolLocalId(MocaRPCProtocol *protocol)
{
  return protocol->protocol.localId().str();
}

const char *MocaRPCProtocolRemoteId(MocaRPCProtocol *protocol)
{
  return protocol->protocol.remoteId().str();
}

int32_t MocaRPCProtocolPendingSize(MocaRPCProtocol *protocol)
{
  return protocol->protocol.pendingSize();
}

void MocaRPCProtocolDestroy(MocaRPCProtocol *protocol)
{
  delete protocol;
}
