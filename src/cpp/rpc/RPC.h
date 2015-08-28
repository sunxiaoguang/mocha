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

enum LogLevel {
  LOG_LEVEL_TRACE = 0,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_FATAL,
  LOG_LEVEL_ASSERT,
};

typedef void (*RPCLogger)(LogLevel level, const char *func, const char *file, uint32_t line, const char *fmt, ...);

END_MOCA_RPC_NAMESPACE

#endif
