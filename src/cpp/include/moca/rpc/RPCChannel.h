#ifndef __MOCA_RPC_CHANNEL_H__
#define __MOCA_RPC_CHANNEL_H__ 1
#include <moca/rpc/RPC.h>

BEGIN_MOCA_RPC_NAMESPACE

typedef void (*RPCEventListener)(RPCChannel *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);

class RPCChannelBuilder;

class RPCChannel : private RPCNonCopyable
{
private:
  friend class RPCChannelBuilder;
  friend class RPCChannelImpl;
private:
  RPCChannelImpl *impl_;

private:
  explicit RPCChannel(RPCChannelImpl *impl);
  ~RPCChannel();

public:
  enum {
    CHANNEL_FLAG_DEFAULT = 0,
    CHANNEL_FLAG_BLOCKING = 1,
    CHANNEL_FLAG_BUFFERED_PAYLOAD = 2,
  };

  class Builder : private RPCNonCopyable
  {
  private:
    friend class RPCChannel;
  private:
    RPCChannelBuilder *impl_;
  private:
    explicit Builder(RPCChannelBuilder *impl);
  public:
    ~Builder();
    Builder *bind(const char *address);
    Builder *bind(const StringLite &address) { return this->bind(address.str()); }
    Builder *connect(const char *address);
    Builder *connect(const StringLite &address) { return this->connect(address.str()); }
    Builder *timeout(int64_t timeout);
    Builder *limit(int32_t size);
    Builder *listener(RPCEventListener listener, RPCOpaqueData userData, int32_t eventMask = EVENT_TYPE_ALL);
    Builder *keepalive(int64_t interval);
    Builder *id(const char *id);
    Builder *logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData = NULL);
    Builder *id(const StringLite &id) { return this->id(id.str()); }
    Builder *flags(int32_t flags);
    Builder *dispatcher(RPCDispatcher *dispatcher);
    Builder *attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor attachmentDestructor);
    RPCChannel *build();
  };

  static Builder *newBuilder();

  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
  int32_t request(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;

  void attachment(RPCOpaqueData attachment, RPCOpaqueDataDestructor destructor);
  RPCOpaqueData attachment() const;
  template<typename T>
  T *attachment() const { return static_cast<T *>(attachment()); }

  void addRef();
  bool release();

  void close();

#ifdef MOCA_RPC_FULL
  int32_t localAddress(string *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(string *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(string *localId) const;
  int32_t remoteId(string *remoteId) const;
  int32_t response(int64_t id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t id, int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int32_t code, const KeyValueMap *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
#endif

};

END_MOCA_RPC_NAMESPACE
#endif
