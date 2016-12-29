#ifndef __MOCA_RPC_H__
#define __MOCA_RPC_H__ 1
#include <moca/rpc/RPCDecl.h>
#include <moca/rpc-c/RPC.h>

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
#include <map>
#include <string>
#endif

BEGIN_MOCA_RPC_NAMESPACE

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
typedef map<string, string> KeyValueMap;
typedef KeyValueMap::iterator KeyValueMapIterator;
typedef KeyValueMap::const_iterator KeyValueMapConstIterator;
#endif

class RPCNonCopyable {
protected:
  RPCNonCopyable() {}
  ~RPCNonCopyable() {}
private:
  RPCNonCopyable(const RPCNonCopyable &other);
  RPCNonCopyable &operator=(const RPCNonCopyable &other);
};

class StringLite {
private:
  char *content_;
  size_t size_;
  size_t capacity_;

private:
  void init() { content_ = NULL; size_ = capacity_ = 0; }

public:
  StringLite() { init(); }
#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
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

  size_t capacity() const
  {
    return capacity_;
  }

  void resize(size_t size);

  void reserve(size_t capacity);

  void shrink(size_t to);

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

#if !defined(MOCA_RPC_LITE) && !defined(MOCA_RPC_NANO)
  void assign(const string &str)
  {
    assign(str.c_str(), str.size());
  }
  void append(const string &str)
  {
    append(str.c_str(), str.size());
  }
#endif

  void append(const char *str, size_t len);

  void release(char **str, size_t *size)
  {
    *str = content_;
    *size = size_;
    content_ = NULL;
    size_ = 0;
    capacity_ = 0;
  }
};

template<typename K, typename V>
struct KeyValuePair {
  K key;
  V value;
  KeyValuePair() {
  }
  KeyValuePair(const K &k, const V &v) {
    key = k;
    value = v;
  }
  KeyValuePair(const KeyValuePair &pair) {
    key = pair.key;
    value = pair.value;
  }
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
      capacity_ = MOCA_RPC_ALIGN(size_ + 1, 16);
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
  KeyValuePairs(const KeyValuePairs &other) {
    *this = other;
  }
  ~KeyValuePairs() { clear(); }

  size_t size() const { return size_; }

  void append(K &key, V &value)
  {
    KeyValuePair<K, V> *pair = addPair();
    pair->key = key;
    pair->value = value;
  }

  void append(const K &key, const V &value)
  {
    KeyValuePair<K, V> *pair = addPair();
    pair->key = key;
    pair->value = value;
  }

  void transfer(StringLite &key, StringLite &value)
  {
    KeyValuePair<K, V> *pair = addPair();
    pair->key.transfer(key);
    pair->value.transfer(value);
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

  KeyValuePairs<K, V> &operator=(const KeyValuePairs<K, V> &other)
  {
    if (this != &other) {
      clear();
      capacity_ = other.size_;
      size_ = other.size_;
      pairs_ = static_cast<KeyValuePair<K, V> *>(malloc(sizeof(KeyValuePair<K, V>) * size_));
      for (size_t idx = 0; idx < size_; ++idx) {
        new (pairs_ + idx) KeyValuePair<K, V>(other.pairs_[idx]);
      }
    }
    return *this;
  }
};

class RPCPacketHeaders : public KeyValuePairs<StringLite, StringLite>
{
public:
  enum {
    INVALID = 0,
    INT8 = 1,
    INT16 = 2,
    INT32 = 3,
    INT64 = 4,
    FLOAT = 5,
    DOUBLE = 6,
    BOOLEAN = 7,
    STRING = 8,
    CSTRING = 9,
    STRING_LITE = 10,
    OPAQUE_DATA = 11,
  };
public:
  static void save(KeyValuePairs<StringLite, StringLite> *headers, ...);
  static void load(const KeyValuePairs<StringLite, StringLite> *headers, ...);
};

inline const char *errorString(int32_t code) { return MocaRPCErrorString(code); }

typedef enum RPCLogLevel {
  RPC_LOG_LEVEL_TRACE = MOCA_RPC_LOG_LEVEL_TRACE,
  RPC_LOG_LEVEL_DEBUG = MOCA_RPC_LOG_LEVEL_DEBUG,
  RPC_LOG_LEVEL_INFO = MOCA_RPC_LOG_LEVEL_INFO,
  RPC_LOG_LEVEL_WARN = MOCA_RPC_LOG_LEVEL_WARN,
  RPC_LOG_LEVEL_ERROR = MOCA_RPC_LOG_LEVEL_ERROR,
  RPC_LOG_LEVEL_FATAL = MOCA_RPC_LOG_LEVEL_FATAL,
  RPC_LOG_LEVEL_ASSERT = MOCA_RPC_LOG_LEVEL_ASSERT,
} RPCLogLevel;

class RPCChannel;
class RPCChannelImpl;
class RPCDispatcher;

typedef MocaRPCOpaqueData RPCOpaqueData;
typedef MocaRPCOpaqueDataDestructor RPCOpaqueDataDestructor;
typedef void (*RPCLogger)(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);
typedef enum MocaRPCEventType EventType;

typedef enum RPCEventType {
  EVENT_TYPE_CHANNEL_CREATED = (MOCA_RPC_EVENT_TYPE_CHANNEL_CREATED),
  EVENT_TYPE_CHANNEL_LISTENING = (MOCA_RPC_EVENT_TYPE_CHANNEL_LISTENING),
  EVENT_TYPE_CHANNEL_CONNECTED = (MOCA_RPC_EVENT_TYPE_CHANNEL_CONNECTED),
  EVENT_TYPE_CHANNEL_ESTABLISHED = (MOCA_RPC_EVENT_TYPE_CHANNEL_ESTABLISHED),
  EVENT_TYPE_CHANNEL_DISCONNECTED = (MOCA_RPC_EVENT_TYPE_CHANNEL_DISCONNECTED),
  EVENT_TYPE_CHANNEL_REQUEST = (MOCA_RPC_EVENT_TYPE_CHANNEL_REQUEST),
  EVENT_TYPE_CHANNEL_RESPONSE = (MOCA_RPC_EVENT_TYPE_CHANNEL_RESPONSE),
  EVENT_TYPE_CHANNEL_PAYLOAD = (MOCA_RPC_EVENT_TYPE_CHANNEL_PAYLOAD),
  EVENT_TYPE_CHANNEL_ERROR = (MOCA_RPC_EVENT_TYPE_CHANNEL_ERROR),
  EVENT_TYPE_CHANNEL_DESTROYED = (MOCA_RPC_EVENT_TYPE_CHANNEL_DESTROYED),
  EVENT_TYPE_SERVER_CREATED = (MOCA_RPC_EVENT_TYPE_SERVER_CREATED),
  EVENT_TYPE_SERVER_DESTROYED = (MOCA_RPC_EVENT_TYPE_SERVER_DESTROYED),
  EVENT_TYPE_ALL = (MOCA_RPC_EVENT_TYPE_ALL),
} RPCEventType;

typedef MocaRPCErrorEventData ErrorEventData;

struct PacketEventData
{
  int64_t id;
  int32_t code;
  int32_t payloadSize;
  const KeyValuePairs<StringLite, StringLite> *headers;
};

typedef PacketEventData RequestEventData;
typedef PacketEventData ResponseEventData;

typedef MocaRPCPayloadEventData PayloadEventData;

typedef void (*RPCSimpleLoggerSinkEntry)(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *message);
typedef struct RPCSimpleLoggerSink {
  RPCSimpleLoggerSinkEntry entry;
  RPCOpaqueData userData;
} RPCSimpleLoggerSink;
void rpcSimpleLogger(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...);

END_MOCA_RPC_NAMESPACE

#endif
