#ifndef __MOCA_RPC_EASY_CHANNEL_INTERNAL_H__
#define __MOCA_RPC_EASY_CHANNEL_INTERNAL_H__

#include "moca/rpc/RPCChannelEasy.h"
#include "RPCChannelNano.h"
#include "RPCNano.h"
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCChannelEasyBuilder : private RPCNonCopyable
{
private:
  enum {
    ADDRESS_INITIALIZED = 1,
  };
private:
  int32_t initializedFields_;
  StringLite id_;
  StringLite address_;
  int64_t timeout_;
  int64_t keepalive_;
  int32_t limit_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  int32_t flags_;

public:
  RPCChannelEasyBuilder() : initializedFields_(0), timeout_(0x7FFFFFFFFFFFFFFFL),
                             keepalive_(0x7FFFFFFFFFFFFFFFL), limit_(0x7FFFFFFFL), logger_(NULL),
                             level_(DEFAULT_LOG_LEVEL), loggerUserData_(NULL), flags_(0)
  {
    uuidGenerate(&id_);
  }

  ~RPCChannelEasyBuilder()
  {
  }

public:
  void connect(const char *address)
  {
    address_.assign(address);
    initializedFields_ |= ADDRESS_INITIALIZED;
  }
  void id(const char *id)
  {
    id_.assign(id);
  }

  void timeout(int64_t timeout)
  {
    timeout_ = timeout;
  }

  void limit(int32_t size)
  {
    limit_ = size;
  }

  void logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
  {
    logger_ = logger;
    level_ = level;
    loggerUserData_ = userData;
  }

  void flags(int32_t flags)
  {
    flags_ = flags;
  }

  void keepalive(int64_t keepalive)
  {
    keepalive_ = keepalive;
  }

  RPCChannelEasy *build();
};

class RPCChannelEasyImpl : private RPCNonCopyable
{
private:
  enum {
    STATE_ESTABLISHED,
    STATE_ERROR,
    STATE_DISCONNECTED,
    STATE_WAIT,
    STATE_DISPATCH,
  };

  struct Packet {
    int32_t payloadSize;
    int64_t id;
    int32_t code;
    bool isResponse;
    KeyValuePairs<StringLite, StringLite> headers;
    StringLite payload;
    Packet() : id(0), code(0), isResponse(false) {
    }
  };

private:
  RPCChannelNano *channel_;
  bool established_;
  mutable int32_t state_;
  mutable Packet packet_;
  mutable size_t currentPacket_;
  mutable KeyValuePairs<int32_t, Packet> packets_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  volatile mutable int32_t status_;
  mutable pthread_mutex_t mutex_;
#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  mutable KeyValueMap headersMap_;
#endif

private:
  static void eventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);
  void onEvent(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData);
  int32_t status() const {
    int32_t status;
    pthread_mutex_lock(&mutex_);
    status = status_;
    pthread_mutex_unlock(&mutex_);
    return status;
  }

  void status(int32_t status) const {
    pthread_mutex_lock(&mutex_);
    status_ = status;
    pthread_mutex_unlock(&mutex_);
  }

public:
  RPCChannelEasyImpl();
  ~RPCChannelEasyImpl();

  int32_t interrupt() const;

  int32_t init(const StringLite &address, const StringLite &id, int64_t timeout, int64_t keepalive,
      int32_t limit, RPCLogger logger, RPCLogLevel level, RPCOpaqueData loggerUserData, int32_t flags);
  int32_t close();
  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  //int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize,
  //                int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const;
  template<typename T>
  int32_t request(int64_t *id, int32_t code, const T *headers, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const T **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const;
  int32_t poll(int64_t *id, int32_t *code, const KeyValuePairs<StringLite, StringLite> **headers, const void **payload, size_t *payloadSize, bool *isResponse) const;
#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t poll(int64_t *id, int32_t *code, const KeyValueMap **headers, const void **payload, size_t *payloadSize, bool *isResponse) const;
#endif
};

END_MOCA_RPC_NAMESPACE
#endif
