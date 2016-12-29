#ifndef __MOCA_RPC_CHANNEL_EASY_H__
#define __MOCA_RPC_CHANNEL_EASY_H__ 1
#include <moca/rpc/RPC.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCChannelEasy;
class RPCChannelEasyImpl;
class RPCChannelEasyBuilder;

class RPCChannelEasy : private RPCNonCopyable
{
private:
  friend class RPCChannelEasyBuilder;
private:
  RPCChannelEasyImpl *impl_;

private:
  explicit RPCChannelEasy(RPCChannelEasyImpl *);

public:
  class Builder : private RPCNonCopyable
  {
  private:
    friend class RPCChannelEasy;
  private:
    RPCChannelEasyBuilder *impl_;
  private:
    explicit Builder(RPCChannelEasyBuilder *impl);
  public:
    ~Builder();
    Builder *connect(const char *address);
    Builder *connect(const StringLite &address) { return this->connect(address.str()); }
    Builder *timeout(int64_t timeout);
    Builder *limit(int32_t size);
    Builder *id(const char *id);
    Builder *id(const StringLite &id) { return this->id(id.str()); }
    Builder *logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData = NULL);
    Builder *flags(int32_t flags);
    Builder *keepalive(int64_t keepalive);
    RPCChannelEasy *build();
  };

public:
  ~RPCChannelEasy();

  static Builder *newBuilder();
  int32_t close();
  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;

  /* overloaded response functions */
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers) const
  {
    return response(id, code, headers, static_cast<const void *>(NULL), 0);
  }
  int32_t response(int64_t id, int32_t code, const void *payload, size_t payloadSize) const
  {
    return response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
  int32_t response(int64_t id, int32_t code) const
  {
    return response(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), static_cast<const void *>(NULL), 0);
  }

  /* overloaded nonblocking request functions */
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers) const
  {
    return request(id, code, headers, static_cast<const void *>(NULL), 0);
  }
  int32_t request(int64_t *id, int32_t code, const void *payload, size_t payloadSize) const
  {
    return request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
  }
  int32_t request(int64_t *id, int32_t code) const
  {
    return request(id, code, static_cast<const KeyValuePairs<StringLite, StringLite> *>(NULL), static_cast<const void *>(NULL), 0);
  }

  /* overloaded nonblocking fire and forget request functions */
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers) const
  {
    int64_t discarded;
    return request(&discarded, code, headers);
  }
  int32_t request(int32_t code, const void *payload, size_t payloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, payload, payloadSize);
  }
  int32_t request(int32_t code) const
  {
    int64_t discarded;
    return request(&discarded, code);
  }

  /* overloaded blocking request functions */
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers,
                  int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders) const
  {
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(id, code, headers, NULL, 0, responseCode, responseHeaders, &discardedPayload, &discardedPayloadSize);
  }
  int32_t request(int64_t *id, int32_t code, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const void **responsePayload, size_t *responsePayloadSize) const
  {
    const KeyValuePairs<StringLite, StringLite> *discarded;
    return request(id, code, NULL, payload, payloadSize, responseCode, &discarded, responsePayload, responsePayloadSize);
  }
  int32_t request(int64_t *id, int32_t code, int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders) const
  {
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(id, code, NULL, NULL, 0, responseCode, responseHeaders, &discardedPayload, &discardedPayloadSize);
  }
  int32_t request(int64_t *id, int32_t code, int32_t *responseCode, const void **responsePayload, size_t *responsePayloadSize) const
  {
    const KeyValuePairs<StringLite, StringLite> *discarded;
    return request(id, code, NULL, NULL, 0, responseCode, &discarded, responsePayload, responsePayloadSize);
  }
  int32_t request(int64_t *id, int32_t code, int32_t *responseCode) const
  {
    const KeyValuePairs<StringLite, StringLite> *discarded;
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(id, code, NULL, NULL, 0, responseCode, &discarded, &discardedPayload, &discardedPayloadSize);
  }

  /* overloaded blocking request functions when request id can be ignored */
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize, responseCode, responseHeaders, responsePayload, responsePayloadSize);
  }
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers,
                  int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, responseCode, responseHeaders);
  }
  int32_t request(int32_t code, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const void **responsePayload, size_t *responsePayloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, payload, payloadSize, responseCode, responsePayload, responsePayloadSize);
  }
  int32_t request(int32_t code, int32_t *responseCode, const KeyValuePairs<StringLite, StringLite> **responseHeaders) const
  {
    int64_t discarded;
    return request(&discarded, code, responseCode, responseHeaders);
  }
  int32_t request(int32_t code, int32_t *responseCode, const void **responsePayload, size_t *responsePayloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, responseCode, responsePayload, responsePayloadSize);
  }
  int32_t request(int32_t code, int32_t *responseCode) const
  {
    int64_t discarded;
    return request(&discarded, code, responseCode);
  }

  /* overloaded polling functions */
  int32_t poll(int64_t *id, int32_t *code, const KeyValuePairs<StringLite, StringLite> **headers, const void **payload, size_t *payloadSize, bool *isResponse) const;
  int32_t poll(int64_t *id, int32_t *code, const KeyValuePairs<StringLite, StringLite> **headers, bool *isResponse) const
  {
    const void *payload;
    size_t payloadSize;
    return poll(id, code, headers, &payload, &payloadSize, isResponse);
  }
  int32_t poll(int64_t *id, int32_t *code, const void **payload, size_t *payloadSize, bool *isResponse) const
  {
    const KeyValuePairs<StringLite, StringLite> *headers;
    return poll(id, code, &headers, payload, payloadSize, isResponse);
  }
  int32_t poll(int64_t *id, int32_t *code, bool *isResponse) const
  {
    const KeyValuePairs<StringLite, StringLite> *headers;
    const void *payload;
    size_t payloadSize;
    return poll(id, code, &headers, &payload, &payloadSize, isResponse);
  }

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  /* overloaded response functions for full implementation */
  int32_t response(int64_t id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const;
  int32_t response(int64_t id, int32_t code, const KeyValueMap *headers) const
  {
    return response(id, code, headers, static_cast<const void *>(NULL), 0);
  }

  /* overloaded nonblocking request functions for full implementation */
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const;
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers) const
  {
    return request(id, code, headers, static_cast<const void *>(NULL), 0);
  }

  /* overloaded nonblocking fire and forget request for full implementation **/
  int32_t request(int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
  int32_t request(int32_t code, const KeyValueMap *headers) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, static_cast<const void *>(NULL), 0);
  }

  /* overloaded blocking request functions for full implementation **/
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const KeyValueMap **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const;
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers, int32_t *responseCode, const KeyValueMap **responseHeaders) const
  {
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(id, code, headers, NULL, 0, responseCode, responseHeaders, &discardedPayload, &discardedPayloadSize);
  }

  /* overloaded blocking request functions when request id can be ignored for full implementation **/
  int32_t request(int32_t code, const KeyValueMap *headers, const void *payload, size_t payloadSize,
                  int32_t *responseCode, const KeyValueMap **responseHeaders, const void **responsePayload, size_t *responsePayloadSize) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize, responseCode, responseHeaders, responsePayload, responsePayloadSize);
  }
  int32_t request(int32_t code, const KeyValueMap *headers, int32_t *responseCode, const KeyValueMap **responseHeaders) const
  {
    int64_t discarded;
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(&discarded, code, headers, NULL, 0, responseCode, responseHeaders, &discardedPayload, &discardedPayloadSize);
  }
  int32_t request(int32_t code, int32_t *responseCode, const KeyValueMap **responseHeaders) const
  {
    int64_t discarded;
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return request(&discarded, code, NULL, NULL, 0, responseCode, responseHeaders, &discardedPayload, &discardedPayloadSize);
  }

  /* overloaded polling functions for full implementation **/
  int32_t poll(int64_t *id, int32_t *code, const KeyValueMap **headers, const void **payload, size_t *payloadSize, bool *isResponse) const;
  int32_t poll(int64_t *id, int32_t *code, const KeyValueMap **headers, bool *isResponse) const
  {
    const void *discardedPayload;
    size_t discardedPayloadSize;
    return poll(id, code, headers, &discardedPayload, &discardedPayloadSize, isResponse);
  }
#endif
  int32_t interrupt() const;
};

END_MOCA_RPC_NAMESPACE

#endif
