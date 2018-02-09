#include "RPCProtocol.h"
#include <zlib.h>

/* TODO make this configurable */
#define COMPRESS_SIZE_THRESHOLD (256)

BEGIN_MOCHA_RPC_NAMESPACE

int32_t
RPCProtocol::serialize(const StringLite &data, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  size_t required = sizeof(int32_t) + data.size() + 1;
  if (required > MAX_INT32) {
    return processErrorHook(RPC_INVALID_ARGUMENT);
  }
  ChainedBuffer *buffer = ChainedBuffer::checkBuffer(head, current, nextBuffer, numBuffers, required);
  *totalSize += required;
  safeWrite<>(static_cast<int32_t>(data.size()), buffer->allocate<int32_t>());
  memcpy(buffer->allocate(data.size()), data.str(), data.size());
  safeWrite<>(static_cast<uint8_t>(0), buffer->allocate<uint8_t>());
  return RPC_OK;
}

int32_t
RPCProtocol::serialize(int64_t id, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  ChainedBuffer *buffer = ChainedBuffer::checkBuffer(head, current, nextBuffer, numBuffers, sizeof(id));
  safeWrite<>(id, buffer->allocate<int64_t>());
  *totalSize += sizeof(id);
  return RPC_OK;
}

int32_t
RPCProtocol::serialize(const KeyValuePairs<StringLite, StringLite> &pairs, ChainedBuffer **head, ChainedBuffer **current, ChainedBuffer ***nextBuffer, int32_t *totalSize, int32_t *numBuffers) const
{
  int32_t code;
  for (size_t idx = 0, size = pairs.size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = pairs.get(idx);
    if (MOCHA_RPC_FAILED(code = serialize(pair->key, head, current, nextBuffer, totalSize, numBuffers))) {
      RPC_LOG_ERROR("Could not serialize key. %d", code);
      return code;
    }
    if (MOCHA_RPC_FAILED(code = serialize(pair->value, head, current, nextBuffer, totalSize, numBuffers))) {
      RPC_LOG_ERROR("Could not serialize value. %d", code);
      return code;
    }
  }

  return RPC_OK;
}

int32_t
RPCProtocol::doSendPacketUnsafe(int64_t id, int32_t code, int32_t type,
    const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  ChainedBuffer *head = NULL;
  ChainedBuffer *buffer = NULL;
  ChainedBuffer **nextBuffer = &head;

  int32_t numBuffers = 0;
  int32_t st = RPC_OK;
  ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers, MOCHA_RPC_ALIGN(sizeof(PacketHeader) + writeOpaqueDataSize_, writeBufferAlignment_));
  buffer->allocate<void>(writeOpaqueDataSize_);
  PacketHeader *hdr = buffer->allocate<PacketHeader>();
  hdr->id = id;
  hdr->code = code;
  hdr->flags = type & 0xFF;
  hdr->payloadSize = payloadSize;
  hdr->headerSize = 0;

  ChainedBuffer *headersHead = NULL;
  ChainedBuffer *headersBuffer;
  ChainedBuffer **headersNextBuffer;
  ChainedBuffer *current;
  int32_t headersNumBuffers;
  int32_t headersSize = 0;
  uint64_t bound;

  if (headers) {
    if (isRemoteAcceptZlib()) {
      z_stream stream;
      headersBuffer = NULL;
      headersNextBuffer = &headersHead;
      headersNumBuffers = 0;
      ChainedBuffer::checkBuffer(&headersHead, &headersBuffer, &headersNextBuffer, &headersNumBuffers, MOCHA_RPC_ALIGN(1, writeBufferAlignment_));
      MOCHA_RPC_DO_GOTO(st, serialize(*headers, &headersHead, &headersBuffer, &headersNextBuffer, &headersSize, &headersNumBuffers), cleanupExit)
      if (headersSize < COMPRESS_SIZE_THRESHOLD) {
        RPC_LOG_DEBUG("Serialized headers size %d is not greater than compression threshold %d, use plain headers", headersSize, COMPRESS_SIZE_THRESHOLD);
        goto fallback;
      }
      memset(&stream, 0, sizeof(stream));
      bound = compressBound(headersSize) + 4;
      ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers, bound);
      current = headersHead;
      if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) == Z_OK) {
        *buffer->get<int32_t>(buffer->size) = headersSize;
        while (current != NULL) {
          stream.next_in = static_cast<Bytef *>(current->buffer);
          stream.avail_in = static_cast<uInt>(current->size);
          stream.next_out = buffer->get<Bytef>(buffer->size + 4 + stream.total_out);
          stream.avail_out = buffer->available() - stream.total_out - 4;
          current = current->next;
          deflate(&stream, current == NULL ? Z_FINISH : Z_NO_FLUSH);
        }
        deflateEnd(&stream);
        /* TODO make these configurable */
        if (stream.total_out + 4 < headersSize * 0.90 || (headersSize > static_cast<int32_t>(stream.total_out) && headersSize - stream.total_out > 128)) {
          RPC_LOG_DEBUG("Compressed size %llu is sufficiently less than original input size %d, use compressed headers", static_cast<unsigned long long>(stream.total_out + 4), headersSize);
          buffer->allocate(stream.total_out + 4);
          ChainedBuffer::destroy(&headersHead);
          hdr->headerSize = static_cast<int32_t>(stream.total_out + 4);
          hdr->flags |= (PACKET_FLAG_HEADER_ZLIB_ENCODED << 8);
          goto payload;
        } else {
          RPC_LOG_DEBUG("Compressed size %llu is not saving significant amount of data compared to original input input size %d, use plain headers", static_cast<unsigned long long>(stream.total_out + 4), headersSize);
        }
      }
fallback:
      *nextBuffer = headersHead;
      buffer = headersBuffer;
      nextBuffer = headersNextBuffer;
      headersHead = NULL;
      hdr->headerSize = headersSize;
    } else {
      MOCHA_RPC_DO_GOTO(st, serialize(*headers, &head, &buffer, &nextBuffer, &hdr->headerSize, &numBuffers), cleanupExit)
    }
  }

payload:
  if (payload && payloadSize > 0) {
    ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers, payloadSize);
    memcpy(buffer->allocate<void>(payloadSize), payload, payloadSize);
  }

  writeSink_(&head, writeSinkUserData_);

cleanupExit:
  if (head) {
    ChainedBuffer::destroy(&head);
  }
  if (headersHead) {
    ChainedBuffer::destroy(&headersHead);
  }

  return st;
}

int32_t
RPCProtocol::sendNegotiation()
{
  ChainedBuffer *head = NULL;
  ChainedBuffer *buffer = NULL;
  ChainedBuffer **nextBuffer = &head;
  int32_t numBuffers = 0;
  int32_t st = RPC_OK;
  ChainedBuffer::checkBuffer(&head, &buffer, &nextBuffer, &numBuffers,
      MOCHA_RPC_ALIGN((sizeof(int32_t) * 3) + localId_.size() + 1 + writeOpaqueDataSize_, writeBufferAlignment_));

  buffer->allocate<void>(writeOpaqueDataSize_);
  NegotiationHeader *header = buffer->allocate<NegotiationHeader>(OFFSET_OF(NegotiationHeader, id) + localId_.size() + 1);
  safeWrite<>(MAGIC_CODE, &header->magicCode);
  safeWrite<>((localFlags_ & NEGOTIATION_FLAG_MASK) << 8 | localVersion_, &header->flags);
  safeWrite<>(static_cast<int32_t>(localId_.size()), &header->idSize);
  memcpy(header->id, localId_.str(), localId_.size() + 1);

  writeSink_(&head, writeSinkUserData_);

  if (head) {
    ChainedBuffer::destroy(&head);
  }

  return st;
}

int32_t
RPCProtocol::doReadPacketHeaderSize()
{
  int32_t st = doRead(&packetHeaderSize_, STATE_PACKET_PAYLOAD_SIZE);
  if (st == RPC_OK && packetHeaderSize_ > limit_) {
    RPC_LOG_WARN("Packet header size %d is greater than limit of %d, close connection now", packetHeaderSize_, limit_);
    shutdown_(channel_);
  }
  return st;
}

void
RPCProtocol::onRead(size_t size)
{
  RPC_LOG_DEBUG("Read %zd bytes of data", size);
  readAvailable_ += size;
  int32_t st = RPC_OK;
  while (st == RPC_OK) {
    switch (state_) {
      case STATE_NEGOTIATION_MAGIC:
        st = doReadNegotiationMagic();
        break;
      case STATE_NEGOTIATION_FLAGS:
        st = doReadNegotiationFlags();
        break;
      case STATE_NEGOTIATION_PEER_ID_SIZE:
        st = doReadNegotiationPeerIdSize();
        break;
      case STATE_NEGOTIATION_PEER_ID:
        st = doReadNegotiationPeerId();
        break;
      case STATE_PACKET_ID:
        st = doReadPacketId();
        break;
      case STATE_PACKET_CODE:
        st = doReadPacketCode();
        break;
      case STATE_PACKET_FLAGS:
        st = doReadPacketFlags();
        break;
      case STATE_PACKET_HEADER_SIZE:
        st = doReadPacketHeaderSize();
        break;
      case STATE_PACKET_PAYLOAD_SIZE:
        st = doReadPacketPayloadSize();
        break;
      case STATE_PACKET_HEADER:
        st = doReadPacketHeader();
        break;
      case STATE_PACKET_PAYLOAD:
        st = doReadPacketPayload();
        break;
      case STATE_START_OVER:
        st = doStartOver();
        break;
    }
  }
  if (st == RPC_WOULDBLOCK) {
    st = RPC_OK;
  }
  if (MOCHA_RPC_FAILED(st)) {
    RPC_LOG_ERROR("Failed to read data. %d:%s", st, errorString(st));
  }
}

template<typename T>
void
RPCProtocol::readPrimitiveSink(void *data, size_t size, void *userData)
{
  PrimitiveSinkArgument<T> *argument = static_cast<PrimitiveSinkArgument<T> *>(userData);
  if (size == sizeof(T)) {
    *argument->result = safeRead<T>(data);
  } else {
    memcpy(unsafeGet<char *>(argument->result, argument->offset), data, size);
    argument->offset += size;
  }
}

int32_t
RPCProtocol::doRead(size_t size, ReadDataSink sink, void *sinkUserData, int32_t nextState)
{
  if (readAvailable_ < size) {
    RPC_LOG_TRACE("There are %zd byte(s) data available when %zd is expected.", readAvailable_, size);
    readPending_ = size - readAvailable_;
    return RPC_WOULDBLOCK;
  }
  readAvailable_ -= size;
  while (size > 0) {
    size_t available = readBuffer_->size - readBufferOffset_;
    if (available > size) {
      available = size;
    }
    sink(readBuffer_->buffer + readBufferOffset_, available, sinkUserData);
    size -= available;
    if (readBufferOffset_ == static_cast<size_t>(readBuffer_->size) && readBuffer_ != writeBuffer_) {
      ChainedBuffer *next = readBuffer_->next;
      free(readBuffer_);
      readBuffer_ = next;
      readBufferOffset_ = 0;
    } else {
      readBufferOffset_ += available;
    }
  }
  if (state_ != nextState) {
    setState(nextState);
  }
  return RPC_OK;
}

int32_t
RPCProtocol::initBuffer()
{
  writeBuffer_ = ChainedBuffer::create(1024, &nextBuffer_);
  return RPC_OK;
}

void
RPCProtocol::readToBufferSink(void *data, size_t size, void *userData)
{
  BufferSinkArgument *argument = static_cast<BufferSinkArgument *>(userData);
  memcpy(unsafeGet<char *>(argument->buffer, argument->offset), data, size);
  argument->offset += size;
}

void
RPCProtocol::readToStringSink(void *data, size_t size, void *userData)
{
  StringLite *string = static_cast<StringLite *>(userData);
  string->append(static_cast<char *>(data), size);
}

int32_t
RPCProtocol::doReadNegotiationMagic()
{
  int32_t magic;
  int32_t st;
  if (MOCHA_RPC_FAILED(st = doRead(&magic, STATE_NEGOTIATION_FLAGS))) {
    return st;
  }
  switch (static_cast<uint32_t>(magic)) {
    case 0x4D4F4341:
    case 0x41434F4D:
      swapByteOrder_ = (static_cast<uint32_t>(magic) != MAGIC_CODE);
      break;
    default:
      return processErrorHook(RPC_INCOMPATIBLE_PROTOCOL);
  }
  return RPC_OK;
}

int32_t
RPCProtocol::doReadNegotiationFlags()
{
  int32_t st;
  if (MOCHA_RPC_FAILED(st = doRead(&remoteFlags_, STATE_NEGOTIATION_PEER_ID_SIZE))) {
    return st;
  }
  remoteVersion_ = remoteFlags_ & 0xFF;
  remoteFlags_ >>= 8;
  return RPC_OK;
}

int32_t
RPCProtocol::doReadNegotiationPeerId()
{
  int32_t st = doRead(remoteIdSize_, &remoteId_, STATE_PACKET_ID);
  if (state_ == STATE_PACKET_ID) {
    st = fireEstablishedEvent();
  }
  return st;
}


int32_t
RPCProtocol::doReadPacketPayloadSize()
{
  int32_t st = doRead(&packetPayloadSize_, STATE_PACKET_HEADER);
  if (state_ == STATE_PACKET_HEADER) {
    if (packetHeaderSize_ < 0 || packetPayloadSize_ < 0) {
      return processErrorHook(RPC_INCOMPATIBLE_PROTOCOL);
    }
    if (packetHeaderSize_ == 0) {
      st = firePacketEvent();
      if (packetPayloadSize_ == 0) {
        setState(STATE_START_OVER);
      } else {
        setState(STATE_PACKET_PAYLOAD);
      }
    }
  }
  return st;
}

int32_t
RPCProtocol::doReadCompressedPacketHeader()
{
  int32_t st;
  int32_t state = state_;
  int32_t originalSize;
  Bytef *readBuffer = static_cast<Bytef *>(malloc(packetHeaderSize_));
  Bytef *decompressedBuffer = NULL;
  char *ptr;
  int32_t size;
  uLongf tmp;
  StringLite key;
  StringLite value;
  MOCHA_RPC_DO_GOTO(st, doRead(packetHeaderSize_, readBuffer, state), cleanupExit);

  if (packetPayloadSize_ == 0) {
    setState(STATE_START_OVER);
  } else {
    setState(STATE_PACKET_PAYLOAD);
  }

  if (packetHeaderSize_ < 4 + 1) {
    st = RPC_CORRUPTED_DATA;
    goto cleanupExit;
  }

  originalSize = safeRead<int32_t>(readBuffer);
  if (originalSize > limit_) {
    st = RPC_CORRUPTED_DATA;
    RPC_LOG_WARN("Decompressed header size %d is greater than limit of %d", originalSize, limit_);
    goto cleanupExit;
  }
  ptr = reinterpret_cast<char *>(decompressedBuffer = static_cast<Bytef *>(malloc(originalSize)));
  tmp = originalSize;
  if (uncompress(decompressedBuffer, &tmp, readBuffer + sizeof(int32_t), packetHeaderSize_ - sizeof(int32_t)) != Z_OK) {
    RPC_LOG_ERROR("Corrupted data from server");
    st = RPC_CORRUPTED_DATA;
    goto cleanupExit;
  }

  while (originalSize > 0) {
    size = safeRead<int32_t>(ptr);
    ptr += sizeof(int32_t);
    key.append(ptr, size);
    ptr += size + 1;
    originalSize -= (size + 1 + sizeof(int32_t));
    size = safeRead<int32_t>(ptr);
    ptr += sizeof(int32_t);
    value.append(ptr, size);
    ptr += size + 1;
    originalSize -= (size + 1 + sizeof(int32_t));
    packetHeaders_.transfer(key, value);
  }

  st = firePacketEvent();
  packetHeaders_.clear();

cleanupExit:
  if (decompressedBuffer) {
    free(decompressedBuffer);
  }
  if (readBuffer) {
    free(readBuffer);
  }

  return st;
}

int32_t
RPCProtocol::doReadPlainPacketHeader()
{
  size_t endReadAvailable = readAvailable_ - packetHeaderSize_;

  int32_t st;
  int32_t size;
  StringLite key;
  StringLite value;
  int32_t state = state_;
  while (readAvailable_ > endReadAvailable) {
    if (MOCHA_RPC_FAILED(st = doRead(&size, state))) {
      return st;
    }
    if (MOCHA_RPC_FAILED(st = doRead(size, &key, state))) {
      return st;
    }
    if (MOCHA_RPC_FAILED(st = doRead(&size, state))) {
      return st;
    }
    if (MOCHA_RPC_FAILED(st = doRead(size, &value, state))) {
      return st;
    }
    packetHeaders_.transfer(key, value);
  }

  st = firePacketEvent();
  packetHeaders_.clear();
  if (packetPayloadSize_ == 0) {
    setState(STATE_START_OVER);
  } else {
    setState(STATE_PACKET_PAYLOAD);
  }
  return st;
}

int32_t
RPCProtocol::doReadPacketHeader()
{
  if (readAvailable_ < static_cast<size_t>(packetHeaderSize_)) {
    return RPC_WOULDBLOCK;
  }

  if (packetFlags_ & PACKET_FLAG_HEADER_ZLIB_ENCODED) {
    return doReadCompressedPacketHeader();
  } else {
    return doReadPlainPacketHeader();
  }
}

int32_t
RPCProtocol::doReadPacketPayload()
{
  int32_t pending = packetPayloadSize_ - dispatchedPayloadSize_;
  readPending_ = pending;
  if (readAvailable_ == 0) {
    return RPC_WOULDBLOCK;
  }

  bool commit = static_cast<size_t>(pending) <= readAvailable_;
  if (static_cast<size_t>(pending) > readAvailable_) {
    pending = readAvailable_;
  }
  FirePayloadEventSinkArgument argument = { this, dispatchedPayloadSize_, packetPayloadSize_, RPC_OK};
  doRead(pending, firePayloadEventSink, &argument, commit ? STATE_START_OVER : state_);
  if (commit) {
    dispatchedPayloadSize_ = 0;
  } else {
    dispatchedPayloadSize_ += pending;
  }
  return argument.status;
}

int32_t
RPCProtocol::doStartOver()
{
  packetId_ = 0;
  packetFlags_ = 0;
  packetHeaderSize_ = 0;
  dispatchedPayloadSize_ = 0;
  setState(STATE_PACKET_ID);
  return RPC_OK;
}

int32_t
RPCProtocol::firePacketEvent()
{
  PacketEventData data = { packetId_, packetCode_, packetPayloadSize_, &packetHeaders_ };
  int32_t eventType;
  switch (packetType_) {
    case PACKET_TYPE_REQUEST:
      eventType = EVENT_TYPE_CHANNEL_REQUEST;
      break;
    case PACKET_TYPE_RESPONSE:
      eventType = EVENT_TYPE_CHANNEL_RESPONSE;
      break;
    case PACKET_TYPE_HINT:
      dispatchHint(&data);
    default:
      return RPC_OK;
  }
  return doFireEvent(eventType, &data);
}

int32_t
RPCProtocol::firePayloadEvent(size_t size, const char *payload, bool commit)
{
  PayloadEventData data = { packetId_, packetCode_, static_cast<int32_t>(size), commit, payload};
  return doFireEvent(EVENT_TYPE_CHANNEL_PAYLOAD, &data);
}

int32_t
RPCProtocol::fireErrorEvent(int32_t status, const char *message) const
{
  if (message == NULL) {
    message = errorString(status);
  }
  ErrorEventData data = { status, message };
  return doFireEvent(EVENT_TYPE_CHANNEL_ERROR, &data);
}

void
RPCProtocol::firePayloadEventSink(void *data, size_t size, void *userData)
{
  FirePayloadEventSinkArgument *argument = static_cast<FirePayloadEventSinkArgument *>(userData);
  RPCProtocol *protocol = argument->protocol;
  argument->dispatched += size;
  bool commit = argument->payloadSize == argument->dispatched;
  if (protocol->isBufferedPayload()) {
    if (commit) {
      if (protocol->payloadBuffer_.size() == 0) {
        argument->status = protocol->firePayloadEvent(size, static_cast<char *>(data), true);
      } else {
        protocol->payloadBuffer_.append(static_cast<char *>(data), size);
        argument->status = protocol->firePayloadEvent(protocol->payloadBuffer_.size(), protocol->payloadBuffer_.str(), true);
        protocol->payloadBuffer_.shrink(0);
      }
    } else {
      protocol->payloadBuffer_.append(static_cast<char *>(data), size);
    }
  } else {
    argument->status = protocol->firePayloadEvent(size, static_cast<char *>(data), commit);
  }
}

void
RPCProtocol::dispatchHint(PacketEventData *data)
{
  return;
}

int32_t
RPCProtocol::doFireEvent(int32_t eventType, RPCOpaqueData eventData) const
{
  listener_(eventType, eventData, listenerUserData_);
  return RPC_OK;
}

int32_t
RPCProtocol::processErrorHook(int32_t status) const
{
  switch (status) {
    case RPC_DISCONNECTED:
    case RPC_CAN_NOT_CONNECT:
    case RPC_INCOMPATIBLE_PROTOCOL:
      shutdown_(channel_);
    default:
      break;
  }
  if (status == RPC_DISCONNECTED) {
    fireDisconnectedEvent();
  } else {
    fireErrorEvent(status, NULL);
  }
  return status;
}

RPCProtocol::RPCProtocol(RPCOpaqueData channel, ShutdownChannel shutdown,
    EventListener listener, RPCOpaqueData listenerUserData,
    WriteDataSink writeSink, RPCOpaqueData writeSinkUserData,
    size_t writeOpaqueDataSize, size_t writeBufferAlignment,
    int32_t state)
  : channel_(channel), shutdown_(shutdown), state_(state), channelFlags_(0),
  writeBuffer_(NULL), readBuffer_(NULL), nextBuffer_(&readBuffer_), readAvailable_(0), readBufferOffset_(0), readPending_(4),
  swapByteOrder_(false), remoteVersion_(0), remoteFlags_(0), remoteIdSize_(0),
  localVersion_(0), localFlags_(0), packetId_(0),
  packetCode_(0), packetFlags_(0), packetType_(PACKET_TYPE_REQUEST), packetHeaderSize_(0),
  packetPayloadSize_(0), dispatchedPayloadSize_(0), logger_(defaultRPCLogger), level_(defaultRPCLoggerLevel),
  loggerUserData_(defaultRPCLoggerUserData), listener_(listener), listenerUserData_(listenerUserData),
  writeSink_(writeSink), writeSinkUserData_(writeSinkUserData), writeOpaqueDataSize_(writeOpaqueDataSize),
  writeBufferAlignment_(writeBufferAlignment), limit_(0)
{
}

RPCProtocol::~RPCProtocol()
{
  if (readBuffer_) {
    ChainedBuffer::destroy(&readBuffer_);
  }
}

ChainedBuffer *
RPCProtocol::checkAndGetBuffer(size_t *size)
{
  int32_t bufferAvailable = writeBuffer_->available();
  size_t suggested = *size;
  bool notEnough = static_cast<size_t>(bufferAvailable) < suggested;
  if (bufferAvailable == 0 || (notEnough && bufferAvailable < 16)) {
    writeBuffer_ = ChainedBuffer::create(MOCHA_RPC_ALIGN(suggested, 4096), &nextBuffer_);
  } else {
    if (notEnough) {
      *size = bufferAvailable;
    }
  }
  return writeBuffer_;
}

int32_t
RPCProtocol::init(const StringLite &id, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, int32_t flags, int32_t limit, int32_t channelFlags)
{
  logger_ = logger;
  level_ = level;
  limit_ = limit;
  loggerUserData_ = loggerUserData;
  localFlags_ = (flags & NEGOTIATION_FLAG_MASK) | NEGOTIATION_FLAG_ACCEPT_ZLIB;
  channelFlags_ = channelFlags;
  int32_t rc;
  localId_ = id;
  if (MOCHA_RPC_FAILED(rc = initBuffer())) {
    RPC_LOG_ERROR("Could not initialize buffer. %d", rc);
    return rc;
  }
  return rc;
}

END_MOCHA_RPC_NAMESPACE
