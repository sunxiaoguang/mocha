#ifndef __MOCA_RPC_LITE_INTERNAL_H__
#define __MOCA_RPC_LITE_INTERNAL_H__ 1

#include "moca/rpc/RPC.h"
#include "uv.h"
#include <poll.h>
#include <pthread.h>

#include "RPCNano.h"

BEGIN_MOCA_RPC_NAMESPACE
int32_t convertUVError(int32_t st, RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line);
#define CONVERT_UV_ERROR(st, uvst, logger, level, userData)                                             \
  do {                                                                                                  \
    st = ::moca::rpc::convertUVError(uvst, logger, level, userData, __FUNCTION__, __FILE__, __LINE__);  \
  } while (false)

template<typename K, typename V, typename H, typename E>
class RPCHashMap : private RPCNonCopyable
{
public:
  typedef void (*Visitor)(const K &key, const V &value, RPCOpaqueData userData);
private:
  struct Entry {
    K key;
    V value;
    Entry *next;
  };

  struct HashTable {
    Entry **buckets;
    uint32_t size;
    uint32_t mask;
    uint32_t count;
  };

private:
  E equals_;
  H hash_;
  uint32_t module_;
  mutable HashTable tables_[2];
  int64_t rehashIndex_;
  mutable RPCMutex mutex_;
private:
  void cleanup() {
    clear();
  }
  void expand(uint32_t size) {
    if (rehashing() || tables_[0].count > size) {
      return;
    }
    size = nextSize(size);

    if (size == tables_[0].size) {
      return;
    }

    HashTable *table;
    if (tables_[0].buckets == NULL) {
      table = tables_;
    } else {
      table = tables_ + 1;
      rehashIndex_ = 0;
    }

    table->size = size;
    table->mask = size - 1;
    table->count = 0;
    table->buckets = static_cast<Entry **>(calloc(1, sizeof(Entry *) * size));
  }
  void checkAndExpand() {
    HashTable *table = tables_;
    if (table->size == 0 || (table->count >= table->size && (table->count / table->size > 4))) {
      expand(table->count * 2);
    }
  }
  void init() {
    expand(1);
  }
  bool rehashing() const { return rehashIndex_ != -1; }
  Entry *doFind(const K &key) const {
    for (size_t idx = 0; idx <= 1; ++idx) {
      HashTable *table = tables_ + idx;
      uint32_t index = hash_(key) & table->mask;
      Entry *entry = table->buckets[index];
      while (entry) {
        if (equals_(entry->key, key)) {
          return entry;
        }
        entry = entry->next;
      }
      if (!rehashing()) {
        break;
      }
    }

    return NULL;
  }
  bool doDelete(const K &key) {
    if (tables_[0].count == 0) {
      return false;
    }

    if (rehashing()) {
      rehashStep();
    }

    for (size_t idx = 0; idx <= 1; idx++) {
      HashTable *table = tables_ + idx;
      uint32_t index = hash_(key) & table->mask;
      Entry *entry = table->buckets[index];
      Entry *prev = NULL;
      while (entry) {
        if (equals_(entry->key, key)) {
          /* Unlink the element from the list */
          if (prev) {
            prev->next = entry->next;
          } else {
            table->buckets[index] = entry->next;
          }
          free(entry);
          table->count--;
          return true;
        }
        prev = entry;
        entry = prev->next;
      }
      if (!rehashing()) {
        return false;
      }
    }

    return false;
  }
  void doClear(HashTable *table) {
    for (int32_t idx = 0, tableSize = table->size; idx < tableSize && table->count > 0; idx++) {
      Entry *entry, *next;
      if (!(entry = table->buckets[idx])) {
        continue;
      }
      while (entry) {
        next = entry->next;
        free(entry);
        table->count--;
        entry = next;
      }
    }
    free(table->buckets);
    memset(table, 0, sizeof(HashTable));
  }
  void add(const K &key, const V &value)
  {
    HashTable *table = rehashing() ? tables_ + 1 : tables_;

    Entry *entry = static_cast<Entry *>(calloc(1, sizeof(Entry)));
    uint32_t index = hash_(key) % table->size;
    entry->next = table->buckets[index];
    entry->key = key;
    entry->value = value;
    table->buckets[index] = entry;
    table->count++;
  }
  void rehashStep() {
    int32_t tries = 10;
    int32_t step = 1;
    HashTable *table = tables_;
    HashTable *nextTable = tables_ + 1;
    while (step-- && table->count != 0) {
      while (table->buckets[rehashIndex_] == NULL) {
        rehashIndex_++;
        if (--tries == 0) {
          return;
        }
      }

      Entry *entry = table->buckets[rehashIndex_];
      while (entry) {
        Entry *next = entry->next;
        uint32_t index = hash_(entry->key) & nextTable->mask;
        entry->next = nextTable->buckets[index];
        nextTable->buckets[index] = entry;
        table->count--;
        nextTable->count++;
        entry = next;
      }

      table->buckets[rehashIndex_++] = NULL;
    }

    if (table->count == 0) {
      free(table->buckets);
      tables_[0] = tables_[1];
      memset(nextTable, 0, sizeof(HashTable));
      rehashIndex_ = -1;
    }
  }

  V *doGet(const K &key) const {
    Entry *entry = doFind(key);
    if (entry) {
      return &entry->value;
    } else {
      return NULL;
    }
  }

  void doVisitAll(HashTable *table, Visitor visitor, RPCOpaqueData userData) {
    if (table->count == 0) {
      return;
    }

    for (int32_t idx = 0, size = table->size; idx < size; ++idx) {
      Entry *entry = table->buckets[idx];
      while (entry) {
        visitor(entry->key, entry->value, userData);
        entry = entry->next;
      }
    }
  }

  static uint32_t nextSize(uint32_t size) {
    if (size >= 0xFFFFFFFF) {
      return 0xFFFFFFFF;
    }
    uint32_t sz = 1024;
    while (true) {
      if (sz > size) {
        return sz;
      }
      sz <<= 1;
    }
  }

public:
  RPCHashMap() : rehashIndex_(-1) {
    memset(&tables_, 0, sizeof(tables_));
    init();
  }
  ~RPCHashMap() {
    cleanup();
  }

  void put(const K &key, const V &value) {
    RPCLock lock(mutex_);
    if (rehashing()) {
      rehashStep();
    } else {
      checkAndExpand();
    }

    Entry *entry = doFind(key);
    if (!entry) {
      add(key, value);
    } else {
      entry->key = key;
    }
  }

  V *get(const K &key) { return doGet(key); }
  const V *get(const K &key) const { return doGet(key); }
  bool remove(const K &key) {
    RPCLock lock(mutex_);
    return doDelete(key);
  }
  void visitAll(Visitor visitor, RPCOpaqueData userData) {
    RPCLock lock(mutex_);
    doVisitAll(tables_, visitor, userData);
    if (rehashing()) {
      doVisitAll(tables_ + 1, visitor, userData);
    }
  }

  size_t size() const {
    RPCLock lock(mutex_);
    return tables_[0].count + tables_[1].count;
  }
  void clear() {
    RPCLock lock(mutex_);
    doClear(tables_);
    doClear(tables_ + 1);
    rehashIndex_ = -1;
  }
};

typedef void (*RPCAsyncTask)(RPCOpaqueData data);

class RPCAsyncQueue : private RPCNonCopyable
{
private:
  typedef void (*RPCAsyncTaskSink)(RPCAsyncTask task, RPCOpaqueData taskUserData, RPCOpaqueData sinkUserData);

  enum AsyncTaskFlags {
    ASYNC_TASK_POOLED = 1,
  };
  struct AsyncTask {
    volatile int32_t flags;
    RPCAsyncTask task;
    RPCOpaqueData data;
    AsyncTask *next;
  };

  struct SubQueue {
    volatile int32_t flags;
    RPCMutex freeMutex;
    AsyncTask *free;
    AsyncTask **freeCurrent;
    struct pollfd poll;
    int32_t pipe[2];
    int32_t pipeSize;
    AsyncTask *pending;
    AsyncTask **pendingCurrent;
    RPCMutex flagsMutex;
    RPCMutex pendingMutex;
  };

private:
  int32_t size_;
  int32_t taskPoolSize_;
  int32_t queueSize_;
  SubQueue *queues_;
  RPCAtomic<int64_t> counter_;
  RPCLogger logger_;
  RPCLogLevel level_;
  RPCOpaqueData loggerUserData_;
  static RPCThreadLocalKey tlsKey_;

private:
  static void initTls();

  SubQueue *queue(int32_t key);
  int32_t doDequeue(SubQueue *queue, RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData, bool blocking);
  int32_t doEnqueue(SubQueue *queue, RPCAsyncTask task, RPCOpaqueData data);

  int32_t initQueue(SubQueue *queue);

public:
  RPCAsyncQueue(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData);
  ~RPCAsyncQueue();

  int32_t init(int32_t numSubQueue, int32_t taskPoolSize);
  void shutdown();
  int32_t enqueue(RPCAsyncTask task, RPCOpaqueData data);
  int32_t enqueue(RPCAsyncTask task, RPCOpaqueData data, int32_t key);

  int32_t dequeue(RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData, int32_t key, bool blocking = true);
  int32_t dequeue(RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData);
};

struct MocaRPCWrapper {
  int32_t refcount;
  MocaRPCKeyValuePairs *headers;
  MocaRPCKeyValuePairs *buffer;
  MocaRPCKeyValuePairs empty[1];
  MocaRPCOpaqueData attachment;
  MocaRPCOpaqueDataDestructor attachmentDestructor;
};

void convert(const MocaRPCKeyValuePairs *from, KeyValuePairs<StringLite, StringLite> *to);
void convertSink(int32_t index, MocaRPCKeyValuePair *to, void *userData);
void convert(const KeyValuePairs<StringLite, StringLite> *from, MocaRPCWrapper *wrapper);
void destroyAttachment(MocaRPCWrapper *wrapper);
void destroy(MocaRPCWrapper *wrapper);

END_MOCA_RPC_NAMESPACE

#endif
