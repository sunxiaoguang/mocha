#include "RPCLite.h"
#include "RPCNano.h"
#include "RPCLogging.h"
#include <sys/eventfd.h>
#include <unistd.h>

BEGIN_MOCA_RPC_NAMESPACE

int32_t convertUVError(int32_t uvst, RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line)
{
  int32_t st;
  switch (uvst) {
    case UV_E2BIG:
    case UV_EAI_BADFLAGS:
    case UV_EAI_BADHINTS:
    case UV_EAI_NODATA:
    case UV_EAI_NONAME:
    case UV_EAI_PROTOCOL:
    case UV_EAI_SERVICE:
    case UV_EBADF:
    case UV_EDESTADDRREQ:
    case UV_EFAULT:
    case UV_EINVAL:
    case UV_EISDIR:
    case UV_EMSGSIZE:
    case UV_ENAMETOOLONG:
    case UV_ENODEV:
    case UV_ENOENT:
    case UV_ENOTDIR:
    case UV_ENOTEMPTY:
    case UV_ENOTSOCK:
    case UV_EPROTO:
    case UV_EPROTOTYPE:
    case UV_ESPIPE:
      st = RPC_INVALID_ARGUMENT;
      break;
    case UV_EACCES:
    case UV_EPERM:
      st = RPC_NO_ACCESS;
      break;
    case UV_EADDRINUSE:
    case UV_EADDRNOTAVAIL:
      st = RPC_CAN_NOT_BIND;
      break;
    case UV_EAGAIN:
    case UV_EAI_AGAIN:
    case UV_ENOBUFS:
    case UV_ENFILE:
    case UV_ERANGE:
    case UV_EMLINK:
      st = RPC_INSUFFICIENT_RESOURCE;
      break;
    case UV_EAFNOSUPPORT:
    case UV_EAI_ADDRFAMILY:
    case UV_EAI_FAMILY:
    case UV_EAI_SOCKTYPE:
    case UV_ENOPROTOOPT:
    case UV_ENOTSUP:
    case UV_EPROTONOSUPPORT:
      st = RPC_NOT_SUPPORTED;
      break;
    case UV_EAI_FAIL:
    case UV_EFBIG:
    case UV_ENOSYS:
      st = RPC_INTERNAL_ERROR;
      break;
    case UV_EAI_MEMORY:
    case UV_ENOMEM:
      st = RPC_OUT_OF_MEMORY;
      break;
    case UV_EAI_OVERFLOW:
      st = RPC_BUFFER_OVERFLOW;
      break;
    case UV_EALREADY:
    case UV_EEXIST:
    case UV_EIO:
    case UV_EISCONN:
    case UV_ELOOP:
    case UV_EAI_CANCELED:
    case UV_ECANCELED:
    case UV_ENOSPC:
    case UV_EROFS:
    case UV_ETXTBSY:
    case UV_EXDEV:
      st = RPC_ILLEGAL_STATE;
      break;
    case UV_EBUSY:
    case UV_ECHARSET:
    case UV_ESRCH:
    case UV_UNKNOWN:
      st = RPC_INTERNAL_ERROR;
      break;
    case UV_ECONNABORTED:
    case UV_ECONNREFUSED:
    case UV_ECONNRESET:
    case UV_EHOSTUNREACH:
    case UV_ENETDOWN:
    case UV_ENETUNREACH:
    case UV_ENONET:
    case UV_ENOTCONN:
    case UV_EPIPE:
    case UV_ESHUTDOWN:
    case UV_ENXIO:
      st = RPC_DISCONNECTED;
      break;
    case UV_EINTR:
    case UV_EOF:
      st = RPC_OK;
      break;
    case UV_EMFILE:
      st = RPC_TOO_MANY_OPEN_FILE;
      break;
    case UV_ETIMEDOUT:
      st = RPC_TIMEOUT;
      break;
    default:
      st = RPC_INTERNAL_ERROR;
      break;
  }

  //LOGGER_ERROR_AT(logger, level, userData, func, file, line, "libuv error %d:%s => %d:%s", uvst, uv_strerror(uvst), st, errorString(st));
  return st;
}

void convert(const MocaRPCKeyValuePairs *from, KeyValuePairs<StringLite, StringLite> *to)
{
  to->clear();
  StringLite tmpKey, tmpValue;
  for (int32_t idx = 0; idx < from->size; ++idx) {
    MocaRPCKeyValuePair *pair = from->pair[idx];
    tmpKey.assign(MocaRPCStringGet(pair->key), pair->key->size);
    tmpValue.assign(MocaRPCStringGet(pair->value), pair->value->size);
    to->transfer(tmpKey, tmpValue);
  }
}

void convertSink(int32_t index, MocaRPCKeyValuePair *to, void *userData)
{
  const KeyValuePair<StringLite, StringLite> *pair = static_cast<KeyValuePairs<StringLite, StringLite> *>(userData)->get(index);
  MocaRPCStringWrapTo(pair->key.str(), static_cast<int32_t>(pair->key.size()), to->key);
  MocaRPCStringWrapTo(pair->value.str(), static_cast<int32_t>(pair->value.size()), to->value);
}

void convert(const KeyValuePairs<StringLite, StringLite> *from, MocaRPCWrapper *wrapper)
{
  size_t size = from->size();
  if (size == 0) {
    wrapper->headers = wrapper->empty;
    return;
  }
  wrapper->buffer = MocaRPCKeyValuePairsWrapToInternal(static_cast<int32_t>(size),
      convertSink, const_cast<KeyValuePairs<StringLite, StringLite> *>(from), wrapper->buffer);
  wrapper->headers = wrapper->buffer;
  wrapper->headers->size = static_cast<int32_t>(size);
  MocaRPCKeyValuePair **headers = wrapper->headers->pair;
  for (size_t idx = 0; idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = from->get(idx);
    MocaRPCStringWrapTo(pair->key.str(), static_cast<int32_t>(pair->key.size()), headers[idx]->key);
    MocaRPCStringWrapTo(pair->value.str(), static_cast<int32_t>(pair->value.size()), headers[idx]->value);
  }
}

void destroyAttachment(MocaRPCWrapper *wrapper)
{
  if (wrapper->attachment && wrapper->attachmentDestructor) {
    wrapper->attachmentDestructor(wrapper->attachment);
  }
  wrapper->attachment = NULL;
  wrapper->attachmentDestructor = NULL;
}

void destroy(MocaRPCWrapper *wrapper)
{
  if (wrapper == NULL) {
    return;
  }

  destroyAttachment(wrapper);
  free(wrapper->buffer);
  free(wrapper);
}

/* TODO make this configurable */
#define FREE_MUTEX_INITIALIZED (1)
#define FLAGS_MUTEX_INITIALIZED (2)
#define PENDING_MUTEX_INITIALIZED (4)
#define RUNNING (8)

RPCAsyncQueue::RPCAsyncQueue(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
  : size_(0), taskPoolSize_(0), queueSize_(0), queues_(NULL), logger_(logger), level_(level), loggerUserData_(userData)
{
  pthread_once(&initTls_, initTls);
}

pthread_key_t RPCAsyncQueue::tlsKey_;
pthread_once_t RPCAsyncQueue::initTls_;

void RPCAsyncQueue::initTls()
{
  pthread_key_create(&tlsKey_, NULL);
}

RPCAsyncQueue::~RPCAsyncQueue()
{
  for (int32_t idx = 0; idx < size_; ++idx) {
    SubQueue *queue = this->queue(idx);
    if (queue->flags & FLAGS_MUTEX_INITIALIZED) {
      uv_mutex_destroy(&queue->flagsMutex);
    }
    if (queue->flags & PENDING_MUTEX_INITIALIZED) {
      uv_mutex_destroy(&queue->pendingMutex);
    }
    if (queue->flags & FREE_MUTEX_INITIALIZED) {
      uv_mutex_destroy(&queue->freeMutex);
    }
  }
  free(queues_);
}

void
RPCAsyncQueue::shutdown()
{
  for (int32_t idx = 0; idx < size_; ++idx) {
    SubQueue *queue = this->queue(idx);
    if ((queue->flags & RUNNING) == 0) {
      continue;
    }
    queue->flags &= ~RUNNING;
    close(queue->pipe[1]);
  }
}

RPCAsyncQueue::SubQueue *
RPCAsyncQueue::queue(int32_t key)
{
  return unsafeGet<SubQueue>(queues_, abs(key % size_) * queueSize_);
}

int32_t
RPCAsyncQueue::initQueue(SubQueue *queue)
{
  if (pipe(queue->pipe) == -1) {
    return RPC_INTERNAL_ERROR;
  }
  /*
  if ((queue->pipeSize = sizeof(AsyncTask *) * fcntl(queue->pipe[1], F_GETPIPE_SZ)) < 0) {
    return RPC_INTERNAL_ERROR;
  }
  */
  queue->pipeSize = 512 * 1024;
  queue->poll.fd = queue->pipe[0];
  queue->poll.events = POLLIN;
  queue->poll.revents = 0;
  if (uv_mutex_init(&queue->freeMutex)) {
    return RPC_INTERNAL_ERROR;
  }
  queue->flags |= FREE_MUTEX_INITIALIZED;
  if (uv_mutex_init(&queue->flagsMutex)) {
    return RPC_INTERNAL_ERROR;
  }
  queue->flags |= FLAGS_MUTEX_INITIALIZED;
  if (uv_mutex_init(&queue->pendingMutex)) {
    return RPC_INTERNAL_ERROR;
  }
  queue->flags |= PENDING_MUTEX_INITIALIZED;

  queue->freeCurrent = &queue->free;
  queue->pendingCurrent = &queue->pending;
  AsyncTask *tasks = unsafeGet<AsyncTask>(queue, sizeof(SubQueue));
  for (int32_t idx = 0; idx < taskPoolSize_; ++idx) {
    AsyncTask *task = tasks + idx;
    task->flags = ASYNC_TASK_POOLED;
    *queue->freeCurrent = task;
    queue->freeCurrent = &task->next;
  }
  queue->flags |= RUNNING;
  return RPC_OK;
}

int32_t
RPCAsyncQueue::doDequeue(SubQueue *queue, RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData, bool blocking)
{
  AsyncTask **tasks = NULL, *head = NULL, **current = &head, *task, *next;
  pthread_setspecific(tlsKey_, queue);
  ssize_t rd;

loop:
  rd = poll(&queue->poll, 1, 0);
  if (rd == 0) {
    uv_mutex_lock(&queue->pendingMutex);
    if ((head = queue->pending) != NULL) {
      head = queue->pending;
      queue->pending = NULL;
      queue->pendingCurrent = &queue->pending;
      rd = 1;
    }
    uv_mutex_unlock(&queue->pendingMutex);
    if (head == NULL) {
      if (!blocking) {
        goto cleanup;
      } else {
        goto readTask;
      }
    }
  } else {
readTask:
    if (tasks == NULL) {
      tasks = static_cast<AsyncTask **>(malloc(queue->pipeSize));
    }
    rd = read(queue->pipe[0], tasks, queue->pipeSize);
    head = NULL;
    current = &head;
    for (ssize_t idx = 0; idx < rd / static_cast<ssize_t>(sizeof(AsyncTask *)); ++idx) {
      task = tasks[idx];
      *current = task;
      current = &task->next;
    }
  }

  task = head;
  head = NULL;
  current = &head;
  while (task) {
    next = task->next;
    sink(task->task, task->data, sinkUserData);
    if ((task->flags & ASYNC_TASK_POOLED) == 0) {
      free(task);
    } else {
      *current = task;
      current = &task->next;
      task->next = NULL;
    }
    task = next;
  }

  if (head) {
    uv_mutex_lock(&queue->freeMutex);
    *queue->freeCurrent = head;
    uv_mutex_unlock(&queue->freeMutex);
  }

  if (rd > 0) {
    goto loop;
  }

cleanup:
  if (tasks != NULL) {
    free(tasks);
  }

  pthread_setspecific(tlsKey_, NULL);
  return RPC_OK;
}

int32_t
RPCAsyncQueue::doEnqueue(SubQueue *queue, RPCAsyncTask task, RPCOpaqueData data)
{
#if 0
  AsyncTask *free;
  if (uv_mutex_trylock(&queue->freeMutex) == UV_EBUSY) {
allocOnHeap:
    /* dynamic allocate on heap */
    free = static_cast<AsyncTask *>(calloc(1, sizeof(AsyncTask)));
  } else {
    /* TODO check this on try path */
    if ((queue->flags & RUNNING) == 0) {
      uv_mutex_unlock(&queue->freeMutex);
      return RPC_ILLEGAL_STATE;
    }
    if ((free = queue->free)) {
      if (queue->freeCurrent == &free->next) {
        queue->freeCurrent = &queue->free;
        queue->free = NULL;
      } else {
        queue->free = free->next;
      }
    }
    uv_mutex_unlock(&queue->freeMutex);
    if (free == NULL) {
      goto allocOnHeap;
    }
  }
#else
  AsyncTask *free;
  SubQueue *caller;
  int32_t st;
  uv_mutex_lock(&queue->flagsMutex);
  st = (queue->flags & RUNNING) == 0 ? RPC_ILLEGAL_STATE : RPC_OK;
  uv_mutex_unlock(&queue->flagsMutex);
  if (st != RPC_OK) {
    return st;
  }
  uv_mutex_lock(&queue->freeMutex);
  if ((free = queue->free)) {
    if (queue->freeCurrent == &free->next) {
      queue->freeCurrent = &queue->free;
      queue->free = NULL;
    } else {
      queue->free = free->next;
    }
  }
  uv_mutex_unlock(&queue->freeMutex);
  if (free == NULL) {
    /* dynamic allocate on heap */
    free = static_cast<AsyncTask *>(calloc(1, sizeof(AsyncTask)));
  }
#endif

  free->task = task;
  free->data = data;
  free->next = NULL;

  if ((caller = static_cast<SubQueue *>(pthread_getspecific(tlsKey_))) != NULL) {
    if (caller != queue) {
      uv_mutex_lock(&queue->pendingMutex);
    }
    /* inside dispatching thread, put task into pending task queue */
    *queue->pendingCurrent = free;
    queue->pendingCurrent = &free->next;
    if (caller != queue) {
      uv_mutex_unlock(&queue->pendingMutex);
    }
  } else {
    write(queue->pipe[1], &free, sizeof(free));
  }

  return RPC_OK;
}

int32_t
RPCAsyncQueue::init(int32_t numSubQueue, int32_t taskPoolSize)
{
  int32_t st = RPC_OK;
  size_ = numSubQueue;
  taskPoolSize_ = taskPoolSize;
  queueSize_ = sizeof(SubQueue) + (sizeof(AsyncTask) * taskPoolSize_);
  queues_ = static_cast<SubQueue *>(calloc(1, static_cast<size_t>(queueSize_) * size_));
  for (int32_t idx = 0; idx < size_; ++idx) {
    if (MOCA_RPC_FAILED(st = initQueue(queue(idx)))) {
      return st;
    }
  }

  return st;
}
int32_t
RPCAsyncQueue::enqueue(RPCAsyncTask task, RPCOpaqueData data)
{
  return enqueue(task, data, static_cast<int32_t>(counter_.add(1) & 0x7FFFFFFF));
}
int32_t
RPCAsyncQueue::enqueue(RPCAsyncTask task, RPCOpaqueData data, int32_t key)
{
  return doEnqueue(queue(key), task, data);
}

int32_t
RPCAsyncQueue::dequeue(RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData, int32_t key, bool blocking)
{
  return doDequeue(queue(key), sink, sinkUserData, blocking);
}
int32_t
RPCAsyncQueue::dequeue(RPCAsyncTaskSink sink, RPCOpaqueData sinkUserData)
{
  int32_t st = RPC_OK;
  int32_t st2;
  for (int32_t idx = 0; idx < size_; ++idx) {
    if ((MOCA_RPC_FAILED(st2 = doDequeue(queue(idx), sink, sinkUserData, false)))) {
      RPC_LOG_ERROR("Could not dequeue async task from sub queue %d. %d:%s", idx, st2, errorString(st2));
      if (st == RPC_OK) {
        st = st2;
      }
    }
  }
  return st;
}

END_MOCA_RPC_NAMESPACE
