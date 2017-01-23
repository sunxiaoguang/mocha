#include "RPCDispatcher.h"

BEGIN_MOCA_RPC_NAMESPACE
RPCDispatcher::Builder::Builder(RPCDispatcherBuilder *impl) : impl_(impl)
{
}

RPCDispatcher::Builder::~Builder()
{
  delete impl_;
}

RPCDispatcher::Builder *
RPCDispatcher::Builder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  impl_->logger(logger, level, userData);
  return this;
}

RPCDispatcher *
RPCDispatcher::Builder::build()
{
  return impl_->build();
}

RPCDispatcher::~RPCDispatcher()
{
}

RPCDispatcher::Builder *
RPCDispatcher::newBuilder()
{
  return new RPCDispatcher::Builder(new RPCDispatcherBuilder());
}

RPCDispatcher::RPCDispatcher(RPCDispatcherImpl *impl) : impl_(impl)
{
}

int32_t
RPCDispatcher::stop()
{
  return impl_->stop();
}

int32_t
RPCDispatcher::run(int32_t flags)
{
  return impl_->run(static_cast<uv_run_mode>(flags));
}

bool
RPCDispatcher::isDispatchingThread() const
{
  return impl_->isDispatchingThread();
}

void
RPCDispatcher::addRef()
{
  impl_->addRef();
}

bool
RPCDispatcher::release()
{
  return impl_->release();
}

static void asyncTaskEntry(RPCOpaqueData userData)
{
  RPCDispatcherAsyncTask *asyncTask = static_cast<RPCDispatcherAsyncTask *>(userData);
  asyncTask->task(asyncTask->dispatcher, asyncTask->userData);
  free(asyncTask);
}

int32_t
RPCDispatcher::submitAsync(AsyncTask task, RPCOpaqueData userData)
{
  RPCDispatcherAsyncTask *asyncTask = static_cast<RPCDispatcherAsyncTask *>(malloc(sizeof(RPCDispatcherAsyncTask)));
  asyncTask->task = task;
  asyncTask->userData = userData;
  asyncTask->dispatcher = this;
  int32_t st = impl_->submitAsync(asyncTaskEntry, asyncTask);
  if (MOCA_RPC_FAILED(st)) {
    free(asyncTask);
  }
  return st;
}

int32_t
RPCDispatcher::create(RPCDispatcher::Poll **poll, RPCDispatcher::Pollable pollable, int32_t events, RPCDispatcher::PollEventListener listener, RPCOpaqueData userData)
{
  RPCDispatcherPoll *newPoll = NULL;
  int32_t st = impl_->createPoll(&newPoll, pollable, events, listener, userData);
  *poll = reinterpret_cast<RPCDispatcher::Poll *>(newPoll);
  return st;
}
int32_t
RPCDispatcher::update(RPCDispatcher::Poll *poll, int32_t events)
{
  return impl_->updatePoll(reinterpret_cast<RPCDispatcherPoll *>(poll), events);
}
int32_t
RPCDispatcher::destroy(Poll *poll)
{
  return impl_->destroyPoll(reinterpret_cast<RPCDispatcherPoll *>(poll));
}

int32_t
RPCDispatcher::create(Timer **timer, int32_t flags, int64_t timeout, TimerEventListener listener, RPCOpaqueData userData)
{
  RPCDispatcherTimer *newTimer = NULL;
  int32_t st = impl_->createTimer(&newTimer, flags, timeout, listener, userData);
  *timer = reinterpret_cast<RPCDispatcher::Timer *>(newTimer);
  return st;
}
int32_t
RPCDispatcher::destroy(Timer *timer)
{
  return impl_->destroyTimer(reinterpret_cast<RPCDispatcherTimer *>(timer));
}

RPCDispatcherBuilder::RPCDispatcherBuilder() : logger_(defaultRPCLogger), level_(defaultRPCLoggerLevel), loggerUserData_(defaultRPCLoggerUserData)
{
}

void
RPCDispatcherBuilder::logger(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
{
  logger_ = logger;
  level_ = level;
  loggerUserData_ = userData;
}

RPCDispatcherBuilder::~RPCDispatcherBuilder()
{
}

RPCDispatcher *
RPCDispatcherBuilder::build()
{
  RPCDispatcherImpl *dispatcher = new RPCDispatcherImpl(logger_, level_, loggerUserData_);
  RPCDispatcher *wrapper = dispatcher->wrap();
  int32_t st = dispatcher->init();
  if (st) {
    RPC_LOG_ERROR("Could not initialize rpc dispatcher");
    delete dispatcher;
    return NULL;
  }
  return wrapper;
}

RPCThreadLocalKey * volatile RPCDispatcherImpl::dispatchingThreadKey_ = NULL;
RPCOnce RPCDispatcherImpl::initTls_;

void
RPCDispatcherImpl::initTls()
{
  dispatchingThreadKey_ = new RPCThreadLocalKey();
  RPC_MEMORY_BARRIER_FULL();
}

RPCThreadLocalKey *
RPCDispatcherImpl::tls()
{
  initTls_.run(initTls);
  return dispatchingThreadKey_;
}

RPCDispatcherImpl::RPCDispatcherImpl(RPCLogger logger, RPCLogLevel level, RPCOpaqueData userData)
  : wrapper_(NULL), logger_(logger), level_(level), loggerUserData_(userData), flags_(0), numCores_(8), asyncQueue_(logger, level, userData), stopped_(false)
{
  memset(&eventLoop_, 0, sizeof(eventLoop_));
}

RPCDispatcherImpl::~RPCDispatcherImpl()
{
  if (flags_ & EVENT_LOOP_INITIALIZED) {
    uv_loop_close(&eventLoop_);
  }
  delete wrapper_;
}

void
RPCDispatcherImpl::onAsyncStop()
{
  uv_close(reinterpret_cast<uv_handle_t *>(&async_), NULL);
  uv_stop(&eventLoop_);
  asyncToken_.finish();
  release();
}

void
RPCDispatcherImpl::onAsyncStop(RPCOpaqueData data)
{
  static_cast<RPCDispatcherImpl *>(data)->onAsyncStop();
}

void
RPCDispatcherImpl::onAsyncTask(uv_async_t *handle)
{
  static_cast<RPCDispatcherImpl *>(handle->data)->onAsyncTask();
}
void
RPCDispatcherImpl::onAsyncTask()
{
  int32_t st;
  if (MOCA_RPC_FAILED(st = asyncQueue_.dequeue(fireAsyncTask, this))) {
    RPC_LOG_ERROR("Could not dequeue async task from queue. %d:%s", st, errorString(st));
  }
}
void
RPCDispatcherImpl::fireAsyncTask(RPCAsyncTask task, RPCOpaqueData taskUserData, RPCOpaqueData sinkUserData)
{
  task(taskUserData);
}

void
RPCDispatcherImpl::onPollEvent(uv_poll_t *handle, int32_t status, int32_t events)
{
  RPCDispatcherPoll *poll = static_cast<RPCDispatcherPoll *>(handle->data);
  poll->listener(poll->dispatcher->wrapper_, reinterpret_cast<RPCDispatcher::Poll *>(poll), poll->pollable, status, events, poll->userData);
}

void
RPCDispatcherImpl::onAsyncPollStart(RPCDispatcherPoll *poll)
{
  int32_t st;
  if ((poll->flags & RPCDispatcherPoll::POLL_FLAG_INITIALIZED) == 0) {
    if ((st = uv_poll_init_socket(&eventLoop_, &poll->handle, poll->pollable))) {
      goto cleanupExit;
    }
    poll->handle.data = poll;
    poll->flags |= RPCDispatcherPoll::POLL_FLAG_INITIALIZED;
  }
  if ((st = uv_poll_start(&poll->handle, poll->events, onPollEvent))) {
    goto cleanupExit;
  }
  return;

cleanupExit:
  CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
  poll->listener(wrapper_, reinterpret_cast<RPCDispatcher::Poll *>(poll), poll->pollable,
      st, RPCDispatcher::POLL_ERROR | RPCDispatcher::POLL_DESTROYED, poll->userData);
  free(poll);
  return;
}

void
RPCDispatcherImpl::onAsyncPollStart(RPCOpaqueData data)
{
  RPCDispatcherPoll *poll = static_cast<RPCDispatcherPoll *>(data);
  poll->dispatcher->onAsyncPollStart(poll);
}

int32_t
RPCDispatcherImpl::createPoll(RPCDispatcherPoll **poll, RPCDispatcher::Pollable pollable, int32_t events,
    RPCDispatcher::PollEventListener listener, RPCOpaqueData userData)
{
  int32_t st;
  RPCDispatcherPoll *tmp = static_cast<RPCDispatcherPoll *>(malloc(sizeof(RPCDispatcherPoll)));
  MOCA_RPC_CHECK_MEMORY(tmp)
  tmp->pollable = pollable;
  tmp->listener = listener;
  tmp->events = events;
  tmp->userData = userData;
  tmp->dispatcher = this;
  tmp->flags = 0;
  memset(&tmp->handle, 0, sizeof(tmp->handle));

  if (MOCA_RPC_FAILED(st = submitAsync(onAsyncPollStart, tmp, (int32_t) pollable))) {
    free(tmp);
  } else {
    *poll = tmp;
  }
  return st;
}

int32_t
RPCDispatcherImpl::updatePoll(RPCDispatcherPoll *poll, int32_t events)
{
  poll->events = events;
  return submitAsync(onAsyncPollStart, poll, (int32_t) poll->pollable);
}

void
RPCDispatcherImpl::onAsyncPollDestroy(RPCDispatcherPoll *poll)
{
  int32_t st;
  if ((st = uv_poll_stop(&poll->handle))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    poll->listener(wrapper_, reinterpret_cast<RPCDispatcher::Poll *>(poll), poll->pollable,
      st, RPCDispatcher::POLL_ERROR, poll->userData);
  }
  uv_close(reinterpret_cast<uv_handle_t *>(&poll->handle), onPollCollect);
  return;
}

void
RPCDispatcherImpl::onPollCollect(uv_handle_t *handle)
{
  RPCDispatcherPoll *poll = static_cast<RPCDispatcherPoll *>(handle->data);
  poll->listener(poll->dispatcher->wrapper_, reinterpret_cast<RPCDispatcher::Poll *>(poll),
      0, RPC_OK, RPCDispatcher::POLL_DESTROYED, poll->userData);
  free(poll);
}

void
RPCDispatcherImpl::onAsyncPollDestroy(RPCOpaqueData data)
{
  RPCDispatcherPoll *poll = static_cast<RPCDispatcherPoll *>(data);
  poll->dispatcher->onAsyncPollDestroy(poll);
}

int32_t
RPCDispatcherImpl::destroyPoll(RPCDispatcherPoll *poll)
{
  MOCA_RPC_CHECK_ARGUMENT(poll)
  return submitAsync(onAsyncPollDestroy, poll, (int32_t) poll->pollable);
}

void
RPCDispatcherImpl::onTimerEvent(uv_timer_t *handle)
{
  RPCDispatcherTimer *timer = static_cast<RPCDispatcherTimer *>(handle->data);
  timer->listener(timer->dispatcher->wrapper_, reinterpret_cast<RPCDispatcher::Timer *>(timer), RPCDispatcher::TIMER_FIRED, timer->userData);
}

void
RPCDispatcherImpl::onAsyncTimerCreate(RPCDispatcherTimer *timer)
{
  int32_t st;
  if ((st = uv_timer_init(&eventLoop_, &timer->handle))) {
    goto cleanupExit;
  }
  timer->handle.data = timer;
  if ((st = uv_timer_start(&timer->handle, onTimerEvent, timer->timeout / 1000, timer->repeat / 1000))) {
    goto cleanupExit;
  }
  return;

cleanupExit:
  CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
  timer->listener(wrapper_, reinterpret_cast<RPCDispatcher::Timer *>(timer),
      RPCDispatcher::TIMER_ERROR | RPCDispatcher::TIMER_DESTROYED, timer->userData);
  free(timer);
  return;
}

void
RPCDispatcherImpl::onAsyncTimerCreate(RPCOpaqueData data)
{
  RPCDispatcherTimer *timer = static_cast<RPCDispatcherTimer *>(data);
  timer->dispatcher->onAsyncTimerCreate(timer);
}

void
RPCDispatcherImpl::onTimerCollect(uv_handle_t *handle)
{
  RPCDispatcherTimer *timer = static_cast<RPCDispatcherTimer *>(handle->data);
  timer->listener(timer->dispatcher->wrapper_, reinterpret_cast<RPCDispatcher::Timer *>(timer),
      RPCDispatcher::TIMER_DESTROYED, timer->userData);
  free(timer);
}

void
RPCDispatcherImpl::onAsyncTimerDestroy(RPCDispatcherTimer *timer)
{
  int32_t st;
  if ((st = uv_timer_stop(&timer->handle))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    timer->listener(wrapper_, reinterpret_cast<RPCDispatcher::Timer *>(timer),
      RPCDispatcher::TIMER_ERROR, timer->userData);
  }
  uv_close(reinterpret_cast<uv_handle_t *>(&timer->handle), onTimerCollect);
  return;
}

void
RPCDispatcherImpl::onAsyncTimerDestroy(RPCOpaqueData data)
{
  RPCDispatcherTimer *timer = static_cast<RPCDispatcherTimer *>(data);
  timer->dispatcher->onAsyncTimerDestroy(timer);
}

int32_t
RPCDispatcherImpl::createTimer(RPCDispatcherTimer **timer, int32_t flags, int64_t timeout, RPCDispatcher::TimerEventListener listener, RPCOpaqueData userData)
{
  int32_t st;
  RPCDispatcherTimer *tmp = static_cast<RPCDispatcherTimer *>(malloc(sizeof(RPCDispatcherTimer)));
  MOCA_RPC_CHECK_MEMORY(tmp)
  tmp->listener = listener;
  tmp->userData = userData;
  tmp->dispatcher = this;
  tmp->timeout = timeout;
  tmp->repeat = ((flags & RPCDispatcher::TIMER_FLAG_REPEAT) == RPCDispatcher::TIMER_FLAG_REPEAT) ? timeout : 0;
  memset(&tmp->handle, 0, sizeof(tmp->handle));

  if (MOCA_RPC_FAILED(st = submitAsync(onAsyncTimerCreate, tmp, static_cast<int32_t>(reinterpret_cast<intptr_t>(tmp) >> 3)))) {
    free(tmp);
  } else {
    *timer = tmp;
  }
  return st;
}

int32_t
RPCDispatcherImpl::destroyTimer(RPCDispatcherTimer *timer)
{
  MOCA_RPC_CHECK_ARGUMENT(timer)
  return submitAsync(onAsyncTimerDestroy, timer, static_cast<int32_t>(reinterpret_cast<intptr_t>(timer) >> 3));
}

int32_t
RPCDispatcherImpl::init()
{
  int32_t st;
  if ((st = uv_loop_init(&eventLoop_))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    return st;
  }
  flags_ |= EVENT_LOOP_INITIALIZED;
  if ((st = uv_async_init(loop(), &async_, onAsyncTask))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    return st;
  }
  async_.data = this;
  uv_cpu_info_t *cpu;
  int32_t ncpu;
  if ((st = uv_cpu_info(&cpu, &ncpu))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
    ncpu = 8;
  } else {
    uv_free_cpu_info(cpu, ncpu);
  }
  numCores_ = ncpu;
  if (MOCA_RPC_FAILED(st = asyncQueue_.init(numCores_, 1024))) {
    return st;
  }
  return asyncToken_.init();
}

int32_t
RPCDispatcherImpl::stop()
{
  if (!stopped_.compareAndSet(false, true)) {
    return RPC_ILLEGAL_STATE;
  }
  addRef();
  asyncToken_.start();
  submitAsync(onAsyncStop, this);
  return asyncToken_.wait();
}

int32_t
RPCDispatcherImpl::submitAsync(RPCAsyncTask task, RPCOpaqueData data)
{
  int32_t st = asyncQueue_.enqueue(task, data);
  if (MOCA_RPC_FAILED(st)) {
    return st;
  }
  if ((st = uv_async_send(&async_))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
  }
  return st;
}

int32_t
RPCDispatcherImpl::submitAsync(RPCAsyncTask task, RPCOpaqueData data, int32_t key)
{
  int32_t st = asyncQueue_.enqueue(task, data, key);
  if (MOCA_RPC_FAILED(st)) {
    return st;
  }
  if ((st = uv_async_send(&async_))) {
    CONVERT_UV_ERROR(st, st, logger_, level_, loggerUserData_);
  }
  return st;
}

RPCDispatcher *
RPCDispatcherImpl::wrap()
{
  return (wrapper_ = new RPCDispatcher(this));
}

bool
RPCDispatcherImpl::isDispatchingThread() const
{
  return tls()->get() != NULL;
}

void
RPCDispatcherImpl::attachDispatchingThread()
{
  tls()->set(this);
}

int32_t
RPCDispatcherImpl::unsafeRun(uv_run_mode mode)
{
  return uv_run(&eventLoop_, mode);
}

int32_t
RPCDispatcherImpl::run(uv_run_mode mode)
{
  int32_t rc;
  tls()->set(this);
  rc = unsafeRun(mode);
  tls()->set(NULL);
  return rc;
}

RPCDispatcherThreadImpl::RPCDispatcherThreadImpl() : thread_(threadEntry, this), dispatcher_(NULL), wrapper_(NULL), running_(true)
{
}

int32_t
RPCDispatcherThreadImpl::start(RPCDispatcher *dispatcher)
{
  int32_t st;
  dispatcher_ = dispatcher;
  if ((st = thread_.start()) == RPC_OK) {
    dispatcher->addRef();
  }
  return st;
}

RPCOpaqueData
RPCDispatcherThreadImpl::threadEntry(RPCOpaqueData argument)
{
  static_cast<RPCDispatcherThreadImpl *>(argument)->threadEntry();
  return NULL;
}

void
RPCDispatcherThreadImpl::threadEntry()
{
  bool running = true;
  signal(SIGPIPE, SIG_IGN);
  RPCDispatcherImpl *dispatcher = FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher_);
  dispatcher->attachDispatchingThread();
  do {
    running = running_.get();
    dispatcher->unsafeRun(static_cast<uv_run_mode>(RPCDispatcher::RUN_FLAG_DEFAULT));
  } while (running);
}

RPCDispatcherThreadImpl::~RPCDispatcherThreadImpl()
{
  if (dispatcher_) {
    dispatcher_->release();
  }
  delete wrapper_;
}

RPCDispatcherThread *
RPCDispatcherThreadImpl::wrap()
{
  return wrapper_ = new RPCDispatcherThread(this);
}

int32_t
RPCDispatcherThreadImpl::interrupt()
{
  int32_t st;
  bool running = running_.get();

  if (!running) {
    st = RPC_OK;
  } else {
    st = dispatcher_->stop();
    if (st == RPC_ILLEGAL_STATE) {
      st = RPC_OK;
    }
    running_.set(false);
  }
  return st;
}

int32_t
RPCDispatcherThreadImpl::join()
{
  return thread_.join();
}

RPCDispatcherThread *
RPCDispatcherThread::create(RPCDispatcher *dispatcher)
{
  RPCDispatcherThreadImpl *thread = new RPCDispatcherThreadImpl;
  RPCDispatcherThread *wrapper = thread->wrap();
  int32_t st;
  if ((st = thread->start(dispatcher))) {
    RPCDispatcherImpl *impl = FriendHelper::getImpl<RPCDispatcherImpl>(dispatcher);
    LOGGER_ERROR(impl->logger(), impl->level(), impl->loggerUserData(), "Could not start rpc dispatching thread. ");
    thread->release();
    return NULL;
  } else {
    return wrapper;
  }
}

RPCDispatcherThread::RPCDispatcherThread(RPCDispatcherThreadImpl *impl) : impl_(impl)
{
}

RPCDispatcherThread::~RPCDispatcherThread()
{
}

int32_t
RPCDispatcherThread::shutdown()
{
  int32_t st = interrupt();
  if (MOCA_RPC_FAILED(st)) {
    return st;
  }

  return join();
}

int32_t
RPCDispatcherThread::interrupt()
{
  return impl_->interrupt();
}

int32_t
RPCDispatcherThread::join()
{
  return impl_->join();
}

void
RPCDispatcherThread::addRef()
{
  return impl_->addRef();
}

bool
RPCDispatcherThread::release()
{
  return impl_->release();
}

END_MOCA_RPC_NAMESPACE
