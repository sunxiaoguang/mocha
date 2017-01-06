#include "RPCChannelEasy.h"
#include <errno.h>
#include <netinet/tcp.h>
#include <time.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <zlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include "RPCLogging.h"

BEGIN_MOCA_RPC_NAMESPACE

RPCChannelEasy::~RPCChannelEasy()
{
  delete impl_;
}

RPCChannelEasy::Builder *
RPCChannelEasy::newBuilder()
{
  return new Builder(new RPCChannelEasyBuilder);
}

RPCChannelEasy::Builder::Builder(RPCChannelEasyBuilder *impl) : impl_(impl)
{
}

RPCChannelEasy::Builder::~Builder()
{
  delete impl_;
}

RPCChannelEasy::Builder *
RPCChannelEasy::Builder::connect(const char *address)
{
  impl_->connect(address);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::timeout(int64_t timeout)
{
  impl_->timeout(timeout);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::keepalive(int64_t keepalive)
{
  impl_->keepalive(keepalive);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::limit(int32_t size)
{
  impl_->limit(size);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::id(const char *id)
{
  impl_->id(id);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  impl_->logger(logger, level, userData);
  return this;
}
RPCChannelEasy::Builder *
RPCChannelEasy::Builder::flags(int32_t flags)
{
  impl_->flags(flags);
  return this;
}
RPCChannelEasy *
RPCChannelEasy::Builder::build()
{
  return impl_->build();
}

RPCChannelEasy::RPCChannelEasy(RPCChannelEasyImpl *impl) : impl_(impl)
{
}

RPCChannelEasy *
RPCChannelEasyBuilder::build()
{
  if ((initializedFields_ & ADDRESS_INITIALIZED) == 0) {
    RPC_LOG_ERROR("Server address is not set");
    return NULL;
  }
  RPCChannelEasyImpl *client = new RPCChannelEasyImpl();
  if (client->init(address_, id_, timeout_, keepalive_, limit_, logger_, level_, loggerUserData_, flags_)) {
    delete client;
    return NULL;
  } else {
    uuidGenerate(&id_);
    return new RPCChannelEasy(client);
  }
}

RPCChannelEasyImpl::RPCChannelEasyImpl() : channel_(NULL), established_(false), state_(STATE_DISCONNECTED),
  currentPacket_(0), logger_(NULL), level_(DEFAULT_LOG_LEVEL), loggerUserData_(NULL), status_(RPC_OK)
{
  pthread_mutex_init(&mutex_, NULL);
}

RPCChannelEasyImpl::~RPCChannelEasyImpl()
{
  if (channel_) {
    channel_->release();
  }
  pthread_mutex_destroy(&mutex_);
}

int32_t
RPCChannelEasy::close()
{
  return impl_->close();
}

int32_t
RPCChannelEasy::localAddress(StringLite *localAddress, uint16_t *port) const
{
  return impl_->localAddress(localAddress, port);
}

int32_t
RPCChannelEasy::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  return impl_->remoteAddress(remoteAddress, port);
}

int32_t
RPCChannelEasy::localId(StringLite *localId) const
{
  return impl_->localId(localId);
}
int32_t
RPCChannelEasy::remoteId(StringLite *remoteId) const
{
  return impl_->remoteId(remoteId);
}
int32_t
RPCChannelEasy::response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->response(id, code, headers, payload, payloadSize);
}
int32_t
RPCChannelEasy::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize);
}
int32_t
RPCChannelEasy::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize,
                int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize, responseCode, responseHeaders, responsePayload, responsePayloadSize);
}
int32_t
RPCChannelEasy::poll(int64_t *id, int32_t *code, const KeyValuePairs<StringLite, StringLite> **headers, const void **payload, size_t *payloadSize, bool *isResponse) const
{
  return impl_->poll(id, code, headers, payload, payloadSize, isResponse);
}
int32_t
RPCChannelEasy::interrupt() const
{
  return impl_->interrupt();
}

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
void convert(const KeyValuePairs<StringLite, StringLite> *input, KeyValueMap *output);
void convert(const KeyValueMap *from, KeyValuePairs<StringLite, StringLite> *to);

int32_t
RPCChannelEasy::response(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return impl_->response(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return impl_->response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t
RPCChannelEasy::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize);
}
int32_t
RPCChannelEasy::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize,
                int32_t *responseCode, const KeyValueMap **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const
{
  return impl_->request(id, code, headers, payload, payloadSize, responseCode, responseHeaders, responsePayload, responsePayloadSize);
}
int32_t
RPCChannelEasy::poll(int64_t *id, int32_t *code, const KeyValueMap **headers, const void **payload, size_t *payloadSize, bool *isResponse) const
{
  return impl_->poll(id, code, headers, payload, payloadSize, isResponse);
}
#endif

void
RPCChannelEasyImpl::eventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  static_cast<RPCChannelEasyImpl *>(userData)->onEvent(channel, eventType, eventData);
}

void
RPCChannelEasyImpl::onEvent(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData)
{
  int32_t oldState = state_;
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_CREATED:
      channel_ = const_cast<RPCChannelNano *>(channel);
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      established_ = true;
      state_ = STATE_ESTABLISHED;
      status_ = RPC_OK;
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      state_ = STATE_DISCONNECTED;
      status_ = RPC_DISCONNECTED;
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      state_ = STATE_ERROR;
      {
        ErrorEventData *error = static_cast<ErrorEventData *>(eventData);
        status_ = error->code;
        RPC_LOG_ERROR("Caught error. %d:%s", error->code, error->message);
      }
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
    case EVENT_TYPE_CHANNEL_RESPONSE:
      {
        PacketEventData *packet = static_cast<PacketEventData *>(eventData);
        packet_.id = packet->id;
        packet_.code = packet->code;
        packet_.payload.shrink(0);
        if (packet->headers) {
          packet_.headers = *packet->headers;
        } else {
          packet_.headers.clear();
        }
        packet_.isResponse = eventType == EVENT_TYPE_CHANNEL_RESPONSE;
        packet_.payloadSize = packet->payloadSize;
        if (packet->payloadSize == 0) {
          state_ = STATE_DISPATCH;
        } else {
          RPC_LOG_DEBUG("Expecting %d bytes payload for packet %lld", packet->payloadSize, static_cast<long long>(packet->id));
        }
      }
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      {
        PayloadEventData *payload = static_cast<PayloadEventData *>(eventData);
        packet_.payload.append(payload->payload, payload->size);
        if (payload->commit) {
          RPC_LOG_DEBUG("Dispatch packet %lld with %zd bytes payload", static_cast<long long>(payload->id), packet_.payload.size());
          state_ = STATE_DISPATCH;
        }
      }
      break;
    default:
      break;
  }

  if (oldState != state_ || state_ == STATE_DISPATCH) {
    if (state_ == STATE_DISPATCH) {
      packets_.append(static_cast<int32_t>(packets_.size()), packet_);
    }
    channel_->breakLoop();
  }
}

int32_t
RPCChannelEasyImpl::close()
{
  return channel_->close();
}

#define CHECK_STATUS              \
  if (status_ != RPC_OK) {        \
    switch (status_) {            \
      case MOCA_RPC_TIMEOUT:      \
      case MOCA_RPC_WOULDBLOCK:   \
      case MOCA_RPC_CANCELED:     \
        status_ = RPC_OK;         \
        break;                    \
      default:                    \
        return status_;           \
    }                             \
  }

int32_t
RPCChannelEasyImpl::localAddress(StringLite *localAddress, uint16_t *port) const
{
  CHECK_STATUS
  return channel_->localAddress(localAddress, port);
}

int32_t
RPCChannelEasyImpl::remoteAddress(StringLite *remoteAddress, uint16_t *port) const
{
  CHECK_STATUS
  return channel_->remoteAddress(remoteAddress, port);
}

int32_t
RPCChannelEasyImpl::localId(StringLite *localId) const
{
  CHECK_STATUS
  return channel_->localId(localId);
}

int32_t
RPCChannelEasyImpl::remoteId(StringLite *remoteId) const
{
  CHECK_STATUS
  return channel_->remoteId(remoteId);
}

int32_t
RPCChannelEasyImpl::response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  CHECK_STATUS
  return channel_->response(id, code, headers, payload, payloadSize);
}

int32_t
RPCChannelEasyImpl::request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
{
  CHECK_STATUS
  return channel_->request(id, code, headers, payload, payloadSize);
}

template<typename T>
int32_t
RPCChannelEasyImpl::request(int64_t *id, int32_t code, const T *headers, const void *payload, size_t payloadSize,
                int32_t *responseCode, const T **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const
{
  CHECK_STATUS
  int32_t st = request(id, code, headers, payload, payloadSize);
  if (MOCA_RPC_FAILED(st)) {
    return st;
  }
  int64_t responseId = 0;
  int64_t requestId = *id;
  bool isResponse;
  while ((st = poll(&responseId, responseCode, responseHeaders, responsePayload, responsePayloadSize, &isResponse)) == MOCA_RPC_OK) {
    if (responseId == requestId && isResponse) {
      break;
    }
    RPC_LOG_WARN("Unexpected %s: id=%lld, code=%d, %zd headers %p, %zd bytes payload %p", isResponse ? "response" : "request", static_cast<long long>(responseId),
        *responseCode, (*responseHeaders)->size(), *responseHeaders, *responsePayloadSize, *responsePayload);
  }
  return st;
}

int32_t
RPCChannelEasyImpl::poll(int64_t *id, int32_t *code, const KeyValuePairs<StringLite, StringLite> **headers, const void **payload, size_t *payloadSize, bool *isResponse) const
{
  CHECK_STATUS
  if (currentPacket_ == packets_.size()) {
    state_ = STATE_WAIT;
    if (currentPacket_ != 0) {
      currentPacket_ = 0;
    }
    packets_.clear();
    while (state_ == STATE_WAIT && status() == RPC_OK) {
      channel_->loop();
    }
  }

  if (state_ != STATE_DISPATCH) {
    MOCA_RPC_DO(status_)
  }
  const Packet &packet = packets_.get(currentPacket_++)->value;
  *isResponse = packet.isResponse;
  *id = packet.id;
  *code = packet.code;
  if (headers != NULL) {
    *headers = &packet.headers;
  }
  if (payload != NULL) {
    *payload = packet.payload.str();
  }
  if (payloadSize != NULL) {
    *payloadSize = packet.payload.size();
  }
  return MOCA_RPC_OK;
}

int32_t
RPCChannelEasyImpl::init(const StringLite &address, const StringLite &id, int64_t timeout, int64_t keepalive,
      int32_t limit, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, int32_t flags)
{
  if (logger) {
    logger_ = logger;
    level_ = level;
    loggerUserData_ = loggerUserData;
  } else {
    logger_ = rpcSimpleLogger;
    level_ = defaultRPCSimpleLoggerLogLevel;
    loggerUserData_ = defaultRPCSimpleLoggerSink;
  }
  channel_ = RPCChannelNanoImpl::create(address, id, timeout, keepalive, flags, limit, logger_, level_,
      loggerUserData_, eventListener, this, 0xFFFFFFFF, NULL, NULL, RPCProtocol::NEGOTIATION_FLAG_NO_HINT);
  if (channel_ == NULL) {
    return RPC_INTERNAL_ERROR;
  }

  while (!established_ && status() == RPC_OK) {
    channel_->loop();
  }

  return status_;
}

int32_t
RPCChannelEasyImpl::interrupt() const
{
  status(RPC_WOULDBLOCK);
  return channel_->breakLoop();
}

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
int32_t
RPCChannelEasyImpl::request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmpHeaders;
    convert(headers, &tmpHeaders);
    return request(id, code, &tmpHeaders, payload, payloadSize);
  } else {
    return request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
}

int32_t
RPCChannelEasyImpl::poll(int64_t *id, int32_t *code, const KeyValueMap **headers, const void **payload, size_t *payloadSize, bool *isResponse) const
{
  const KeyValuePairs<StringLite, StringLite> *tmpHeaders = NULL;
  int32_t st = poll(id, code, &tmpHeaders, payload, payloadSize, isResponse);
  if (st == RPC_OK) {
    convert(tmpHeaders, &headersMap_);
    *headers = &headersMap_;
  } else {
    *headers = NULL;
  }
  return st;
}
#endif

END_MOCA_RPC_NAMESPACE
