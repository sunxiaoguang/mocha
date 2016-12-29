#ifndef __MOCA_RPC_CHANNEL_NANO_H__
#define __MOCA_RPC_CHANNEL_NANO_H__ 1
#include <moca/rpc/RPC.h>

BEGIN_MOCA_RPC_NAMESPACE

class RPCChannelNanoImpl;
class RPCChannelNanoBuilder;

class RPCChannelNano : private RPCNonCopyable
{
private:
  friend class RPCChannelNanoImpl;
private:
  RPCChannelNanoImpl *impl_;
private:
  RPCChannelNano(RPCChannelNanoImpl *impl);
  ~RPCChannelNano();

public:
  enum {
    LOOP_FLAG_ONE_SHOT = 1,
    LOOP_FLAG_NONBLOCK = 2,
  };
  typedef void (*EventListener)(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);

  class Builder : private RPCNonCopyable
  {
  private:
    friend class RPCChannelNano;
  private:
    RPCChannelNanoBuilder *impl_;
  private:
    explicit Builder(RPCChannelNanoBuilder *impl);
  public:
    ~Builder();
    Builder *connect(const char *address);
    Builder *connect(const StringLite &address) { return this->connect(address.str()); }
    Builder *timeout(int64_t timeout);
    Builder *limit(int32_t size);
    Builder *listener(EventListener listener, RPCOpaqueData userData, int32_t eventMask = EVENT_TYPE_ALL);
    Builder *keepalive(int64_t interval);
    Builder *id(const char *id);
    Builder *logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData = NULL);
    Builder *id(const StringLite &id) { return this->id(id.str()); }
    Builder *flags(int32_t flags);
    Builder *attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
    RPCChannelNano *build();
  };
public:
  static Builder *newBuilder();

  int32_t close();
  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t loop(int32_t flags = 0);
  int32_t breakLoop() const;
  int32_t keepalive() const;
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }

  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor);
  RPCOpaqueData attachment() const;
  template<typename T>
  T *attachment() const { return static_cast<T *>(attachment()); }

  void addRef();
  bool release();
#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  int32_t response(int64_t id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
#endif
};

END_MOCA_RPC_NAMESPACE

#endif
