#ifndef __MOCA_RPC_H__
#define __MOCA_RPC_H__ 1
#include "RPCDecl.h"

#ifdef MOCA_RPC_HAS_STL
#include <map>
#include <string>
#endif

BEGIN_MOCA_RPC_NAMESPACE

#ifdef MOCA_RPC_HAS_STL
typedef map<string, string> KeyValueMap;
typedef KeyValueMap::iterator KeyValueMapIterator;
typedef KeyValueMap::const_iterator KeyValueMapConstIterator;
#endif

class StringLite {
private:
  char *content_;
  size_t size_;
  size_t capacity_;

private:
  void init() { content_ = NULL; size_ = capacity_ = 0; }

public:
  StringLite() { init(); }
#ifdef MOCA_RPC_HAS_STL
  StringLite(const string &str) { init(); assign(str.data(), str.size()); }
#endif
  StringLite(const char *str) { init(); assign(str); }
  StringLite(const StringLite& other) { init(); assign(other.content_, other.size_); }
  ~StringLite()
  {
    free(content_);
  }

  bool operator==(const StringLite &other)
  {
    return size_ == other.size_ && memcmp(content_, other.content_, size_) == 0;
  }

  StringLite &operator=(const StringLite &other)
  {
    if (this != &other) {
      assign(other.content_, other.size_);
    }
    return *this;
  }

  const char *str() const
  {
    return content_;
  }

  size_t size() const
  {
    return size_;
  }

  void resize(size_t size);

  void assign(const char *str)
  {
    assign(str, strlen(str));
  }

  void transfer(StringLite &other)
  {
    free(content_);
    content_ = other.content_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    other.content_ = NULL;
    other.size_ = 0;
    other.capacity_ = 0;
  }

  void assign(const char *str, size_t len);

  void append(const char *str)
  {
    append(str, strlen(str));
  }

  void append(const char *str, size_t len);
};

template<typename K, typename V>
struct KeyValuePair {
  K key;
  V value;
};

template<typename K, typename V>
class KeyValuePairs
{
private:
  KeyValuePair<K, V> *pairs_;
  size_t size_;
  size_t capacity_;

private:
  KeyValuePair<K, V> *addPair()
  {
    if (size_ == capacity_) {
      capacity_ = MOCA_ALIGN(size_ + 1, 16);
      pairs_ = static_cast<KeyValuePair<K, V> *>(realloc(pairs_, capacity_ * sizeof(KeyValuePair<K, V>)));
      for (size_t idx = size_; idx < capacity_; ++idx) {
        new (pairs_ + idx) KeyValuePair<K, V>;
      }
    }
    return pairs_ + (size_++);
  }

  KeyValuePair<K, V> *doGet(size_t index)
  {
    if (index >= size_) {
      return NULL;
    } else {
      return pairs_ + index;
    }
  }

  V *doGet(const K &key)
  {
    for (size_t idx = 0; idx < size_; ++idx) {
      if (pairs_[idx].key == key) {
        return &(pairs_[idx].value);
      }
    }
    return NULL;
  }

public:
  KeyValuePairs() : pairs_(NULL), size_(0), capacity_(0) { }
  ~KeyValuePairs() { clear(); }

  size_t size() const { return size_; }

  void append(K &key, V &value)
  {
    KeyValuePair<K, V> *pair = addPair();
    pair->key.transfer(key);
    pair->value.transfer(value);
  }

  void append(const K &key, const V &value)
  {
    KeyValuePair<K, V> *pair = addPair();
    pair->key = key;
    pair->value = value;
  }

  KeyValuePair<K, V> *get(size_t index)
  {
    return doGet(index);
  }

  const KeyValuePair<K, V> *get(size_t index) const
  {
    return const_cast<KeyValuePairs<K, V> *>(this)->doGet(index);
  }

  V *get(const K &key)
  {
    return doGet(key);
  }

  const V *get(const K &key) const
  {
    return const_cast<KeyValuePairs<K, V> *>(this)->doGet(key);
  }

  void clear()
  {
    for (size_t idx = 0; idx < size_; ++idx) {
      pairs_[idx].~KeyValuePair<K, V>();
    }
    size_ = 0;
    if (pairs_) {
      free(pairs_);
      pairs_ = NULL;
      capacity_ = 0;
    }
  }
};

const char *errorString(int32_t code);

class RPCClientImpl;
class RPCClient;

typedef void *RPCOpaqueData;
typedef void (*EventListener)(const RPCClient *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData);

enum {
  EVENT_TYPE_CONNECTED = 1 << 0,
  EVENT_TYPE_ESTABLISHED = 1 << 1,
  EVENT_TYPE_DISCONNECTED = 1 << 2,
  EVENT_TYPE_REQUEST = 1 << 3,
  EVENT_TYPE_RESPONSE = 1 << 4,
  EVENT_TYPE_PAYLOAD = 1 << 5,
  EVENT_TYPE_ERROR = 1 << 6,
  EVENT_TYPE_ALL = 0xFFFFFFFF,
};

struct ErrorEventData
{
  int32_t code;
  StringLite message;
};

struct PacketEventData
{
  int64_t id;
  int32_t code;
  int32_t payloadSize;
  const KeyValuePairs<StringLite, StringLite> *headers;
};

typedef PacketEventData RequestEventData;
typedef PacketEventData ResponseEventData;

struct PayloadEventData
{
  int64_t id;
  int32_t size;
  int32_t reserved:31;
  int32_t commit:1;
  const char *payload;
};

class RPCClient
{
private:
  RPCClientImpl *impl_;
private:
  RPCClient();

public:
  enum {
    CONNECT_FLAG_SSL = 1,
    CONNECT_FLAG_DEBUG = 2,
  };

  enum {
    LOOP_FLAG_ONE_SHOT = 1,
    LOOP_FLAG_NONBLOCK = 2,
  };

public:
  ~RPCClient();

  static RPCClient *create(int64_t timeout = 0x7FFFFFFFFFFFFFFFL, int64_t keepalive = 0x7FFFFFFFFFFFFFFFL, int32_t flags = 0);
  int32_t addListener(EventListener listener, RPCOpaqueData userData = NULL, int32_t eventMask = 0xFFFFFFFF);
  int32_t removeListener(EventListener listener);
  int32_t connect(const char *address);
  int32_t close();
  int32_t localAddress(StringLite *localAddress, uint16_t *port = NULL) const;
  int32_t remoteAddress(StringLite *remoteAddress, uint16_t *port = NULL) const;
  int32_t localId(StringLite *localId) const;
  int32_t remoteId(StringLite *remoteId) const;
  int32_t loop(int32_t flags = 0);
  int32_t breakLoop();
  int32_t keepalive();
  int32_t response(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int64_t *id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const;
  int32_t request(int32_t code, const KeyValuePairs<StringLite, StringLite> *headers = NULL, const void *payload = NULL, size_t payloadSize = 0) const
  {
    int64_t discarded;
    return request(&discarded, code, headers, payload, payloadSize);
  }
#ifdef MOCA_RPC_HAS_STL
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
