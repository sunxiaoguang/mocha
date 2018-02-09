#include "com_mocha_rpc_protocol_impl_JniChannel.h"
#include "com_mocha_rpc_protocol_impl_JniChannelNano.h"
#include "com_mocha_rpc_protocol_impl_JniChannelEasy.h"
#include <mocha/rpc/RPCChannelNano.h>
#include <mocha/rpc/RPCChannelEasy.h>
#include <pthread.h>
#ifdef ANDROID
#include <android/log.h>
#else
#include <assert.h>
#endif
#include <stdio.h>

#define RPC_JAVA_EXCEPTION (-1024)
#define MAX_LOG_LINE_SIZE (4096)
namespace mocha { namespace rpc {
JNIEnv *jniEnv;
}}

using namespace std;
using namespace mocha::rpc;

struct TLSContext
{
  JNIEnv *env;
  TLSContext(JNIEnv *e) : env(e) { }
};

pthread_key_t tlsContext;

static jclass runtimeExceptionClass = NULL;
static jclass interruptedExceptionClass = NULL;
static jclass stringClass = NULL;
static jclass keyValuePairClass = NULL;
static jclass channelClass = NULL;
static jclass channelNanoClass = NULL;
static jclass channelEasyClass = NULL;
static jclass channelEasyRequestClass = NULL;
static jclass channelEasyResponseClass = NULL;

static jmethodID keyValuePairConstructorMethod = NULL;
static jmethodID keyValuePairGetKeyMethod = NULL;
static jmethodID keyValuePairGetValueMethod = NULL;
static jmethodID channelNanoDispatchRequestMethod = NULL;
static jmethodID channelNanoDispatchResponseMethod = NULL;
static jmethodID channelNanoDispatchPayloadMethod = NULL;
static jmethodID channelNanoDispatchConnectedMethod = NULL;
static jmethodID channelNanoDispatchEstablishedMethod = NULL;
static jmethodID channelNanoDispatchDisconnectedMethod = NULL;
static jmethodID channelNanoDispatchErrorMethod = NULL;
static jmethodID channelToStringMethod = NULL;
static jmethodID channelEasyRequestConstructorMethod = NULL;
static jmethodID channelEasyResponseConstructorMethod = NULL;
static jfieldID channelHandleField = NULL;
static jfieldID channelGlobalRefField = NULL;

static bool debugMode = true;

class TlsContextHelper
{
private:
  TLSContext context_;
  bool cleanup_;
public:
  TlsContextHelper(JNIEnv *env) : context_(env), cleanup_(false)
  {
    if (!pthread_getspecific(tlsContext)) {
      pthread_setspecific(tlsContext, &context_);
      cleanup_ = true;
    }
  }
  ~TlsContextHelper()
  {
    if (cleanup_) {
      pthread_setspecific(tlsContext, NULL);
    }
  }
};

#define CHECK_AND_INIT_TLS_CONTEXT

#ifdef ANDROID
static void jniLoggerInternal(RPCLogLevel level, const char *tag, const char *log)
{
  if (level == RPC_LOG_LEVEL_ASSERT) {
    __android_log_assert("", tag, "%s", log);
  } else {
    __android_log_print((android_LogPriority) (level + (ANDROID_LOG_VERBOSE - RPC_LOG_LEVEL_TRACE)), tag, "%s", log);
  }
}
#else
static void jniLoggerInternal(RPCLogLevel level, const char *tag, const char *log)
{
  time_t now = time(NULL);
  char timeBuffer[32];
  static char levels[] = {'T', 'D', 'I', 'W', 'E', 'F', 'A'};
  ctime_r(&now, timeBuffer);
  timeBuffer[strlen(timeBuffer) - 1] = '\0';

  printf("%s [%c:%s\n", timeBuffer, levels[level], log + 1);
  assert(level != RPC_LOG_LEVEL_ASSERT);
}
#endif
static void jniLoggerV(RPCLogLevel level, const char *tag, const char *func, const char *file, uint32_t line, const char *fmt, va_list ap)
{
  int32_t bufferSize = MAX_LOG_LINE_SIZE;
  char *buffer = static_cast<char *>(malloc(bufferSize));
  char *p = buffer;
  int32_t size;

  size = snprintf(p, bufferSize, "[%s|%s:%u] ", func, file, line);
  p += size;
  bufferSize -= size;

  if (bufferSize > 0) {
    size = vsnprintf(p, bufferSize, fmt, ap);
    p += size;
    bufferSize -= size;
  }

  if (bufferSize == 0) {
    *(--p) = '\0';
  }
  jniLoggerInternal(level, tag, buffer);
  free(buffer);
}

static void jniLogger(RPCLogLevel level, const char *tag, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  jniLoggerV(level, tag, func, file, line, fmt, args);
  va_end(args);
}

static void rpcLogger(RPCLogLevel level, void *opaque, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  jniLoggerV(level, "RPC", func, file, line, fmt, args);
  va_end(args);
}

#define LOG_TRACE(...) jniLogger(RPC_LOG_LEVEL_TRACE, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) jniLogger(RPC_LOG_LEVEL_DEBUG, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) jniLogger(RPC_LOG_LEVEL_INFO, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...) jniLogger(RPC_LOG_LEVEL_WARN, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) jniLogger(RPC_LOG_LEVEL_ERROR, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) jniLogger(RPC_LOG_LEVEL_FATAL, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ASSERT(...) jniLogger(RPC_LOG_LEVEL_ASSERT, "RPC.jni", __FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

const char *jniErrorString(int32_t code)
{
  if (code == RPC_JAVA_EXCEPTION) {
    return "Java Exception";
  } else {
    return errorString(code);
  }
}


static bool checkException(JNIEnv *env)
{
  if (env->ExceptionCheck()) {
    if (debugMode) {
      env->ExceptionDescribe();
    }
    env->ExceptionClear();
    LOG_ERROR("Caught java exception");
    return true;
  } else {
    return false;
  }
}

static int32_t checkExceptionCode(JNIEnv *env)
{
  if (checkException(env)) {
    return RPC_JAVA_EXCEPTION;
  } else {
    return RPC_OK;
  }
}

static int32_t convert(JNIEnv *env, const char *src, size_t size, jbyteArray *result)
{
  jbyteArray array = env->NewByteArray(static_cast<jsize>(size));
  if (array == NULL) {
    LOG_ERROR("Running out of memory when allocating %zd bytes bytes array", size);
    return RPC_OUT_OF_MEMORY;
  }
  env->SetByteArrayRegion(array, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte *>(src));
  *result = array;
  return RPC_OK;
}

static int32_t convert(JNIEnv *env, jstring src, StringLite *address)
{
  if (checkException(env)) {
    return RPC_JAVA_EXCEPTION;
  }
  const char *str = env->GetStringUTFChars(src, NULL);
  if (str) {
    address->assign(str);
    env->ReleaseStringUTFChars(src, str);
  }
  return checkExceptionCode(env);
}

static int32_t convert(JNIEnv *env, const char *src, jstring *str)
{
  if (checkException(env)) {
    return RPC_JAVA_EXCEPTION;
  }
  jbyteArray bytes = NULL;
  int32_t code = convert(env, src, strlen(src), &bytes);
  if (MOCHA_RPC_FAILED(code)){
    LOG_ERROR("Can not convert '%s' to Java string", str);
    return code;
  }

  *str = static_cast<jstring>(env->CallStaticObjectMethod(channelClass, channelToStringMethod, bytes));
  env->DeleteLocalRef(bytes);
  return checkExceptionCode(env);
}

static int32_t convert(JNIEnv *env, const StringLite &src, jstring *str)
{
  return convert(env, src.str(), str);
}

static int32_t convert(JNIEnv *env, const KeyValuePairs<StringLite, StringLite> *pairs, jobjectArray *newArray)
{
  int32_t code;
  jobjectArray array = env->NewObjectArray(static_cast<jsize>(pairs->size()), keyValuePairClass, NULL);
  jstring key = NULL;
  jstring value = NULL;
  jobject jpair = NULL;
  for (size_t idx = 0, size = pairs->size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = pairs->get(idx);
    MOCHA_RPC_DO_GOTO(code, convert(env, pair->key, &key), error)
    MOCHA_RPC_DO_GOTO(code, convert(env, pair->value, &value), error)
    jobject jpair = env->NewObject(keyValuePairClass, keyValuePairConstructorMethod, key, value);
    MOCHA_RPC_DO_GOTO(code, checkExceptionCode(env), error)
    env->SetObjectArrayElement(array, idx, jpair);
    env->DeleteLocalRef(jpair);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(value);
    jpair = key = value = NULL;
  }

  *newArray = array;
  return RPC_OK;

error:
  if (key) {
    env->DeleteLocalRef(key);
  }
  if (value) {
    env->DeleteLocalRef(value);
  }
  if (jpair) {
    env->DeleteLocalRef(jpair);
  }
  if (array) {
    env->DeleteLocalRef(array);
  }
  return code;
}

static int32_t convert(JNIEnv *env, jobjectArray headers, KeyValuePairs<StringLite, StringLite> *pairs)
{
  StringLite k, v;
  for (jsize idx = 0, size = env->GetArrayLength(headers); idx < size; ++idx) {
    jobject pair = env->GetObjectArrayElement(headers, idx);
    if (!pair) {
      LOG_ERROR("Could not get key value pair out of array");
      return RPC_INVALID_ARGUMENT;
    }
    jobject key = env->CallObjectMethod(pair, keyValuePairGetKeyMethod);
    if (!key) {
      LOG_ERROR("Could not get key out of key value pair");
      return RPC_INVALID_ARGUMENT;
    }
    jobject value = env->CallObjectMethod(pair, keyValuePairGetValueMethod);
    if (!value) {
      LOG_ERROR("Could not get value out of key value pair");
      return RPC_INVALID_ARGUMENT;
    }
    MOCHA_RPC_DO(convert(env, static_cast<jstring>(key), &k));
    MOCHA_RPC_DO(convert(env, static_cast<jstring>(value), &v));
    pairs->append(k, v);
  }

  return RPC_OK;
}

static int32_t convert(JNIEnv *env, int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers,
  const void *payload, size_t payloadSize, bool isResponse, jobject *result)
{
  int32_t st = RPC_OK;
  jbyteArray realPayload = NULL;
  jobjectArray realHeaders = NULL;
  jclass type;
  jmethodID ctor;

  if (isResponse) {
    type = channelEasyResponseClass;
    ctor = channelEasyResponseConstructorMethod;
  } else {
    type = channelEasyRequestClass;
    ctor = channelEasyRequestConstructorMethod;
  }

  if (payload && payloadSize > 0) {
    realPayload = env->NewByteArray(static_cast<jsize>(payloadSize));
    if (realPayload == NULL) {
      LOG_ERROR("Running out of memory when allocating %zd bytes bytes array", payloadSize);
      st = RPC_OUT_OF_MEMORY;
      goto cleanupExit;
    }
    env->SetByteArrayRegion(realPayload, 0, static_cast<jsize>(payloadSize), reinterpret_cast<const jbyte *>(payload));
  }
  if (headers != NULL && MOCHA_RPC_FAILED(st = convert(env, headers, &realHeaders))) {
    goto cleanupExit;
  }

  *result = env->NewObject(type, ctor, id, code, realHeaders, realPayload);
  if (MOCHA_RPC_FAILED(st = checkExceptionCode(env))) {
    goto cleanupExit;
  }

  return RPC_OK;
cleanupExit:
  if (realHeaders) {
    env->DeleteLocalRef(realHeaders);
  }
  if (realPayload) {
    env->DeleteLocalRef(realPayload);
  }
  return st;
}

static void runtimeException(JNIEnv *env, int32_t code)
{
  if (MOCHA_RPC_FAILED(code) && code != RPC_JAVA_EXCEPTION) {
    env->ThrowNew(runtimeExceptionClass, jniErrorString(code));
  }
}

static void interruptedException(JNIEnv *env)
{
  env->ThrowNew(interruptedExceptionClass, "Interrupted Exception");
}

static int32_t
getClass(JNIEnv *env, const char *classname, jclass *clz)
{
  *clz = env->FindClass(classname);
  return checkExceptionCode(env);
}

static int32_t
getClassGlobal(JNIEnv *env, const char *classname, jclass *clz)
{
  jclass clazz;

  MOCHA_RPC_DO(getClass(env, classname, &clazz))
  *clz = reinterpret_cast<jclass>(env->NewGlobalRef(clazz));
  env->DeleteLocalRef(clazz);
  return checkExceptionCode(env);
}

static int32_t
getMethodID(JNIEnv *env, jclass clz, const char *name, const char *type, jmethodID *method)
{
  *method = env->GetMethodID(clz, name, type);
  return checkExceptionCode(env);
}

static int32_t
getStaticMethodID(JNIEnv *env, jclass clz, const char *name, const char *type, jmethodID *method)
{
  *method = env->GetStaticMethodID(clz, name, type);
  return checkExceptionCode(env);
}

static int32_t
getFieldID(JNIEnv *env, jclass clz, const char *name, const char *type, jfieldID *field)
{
  *field = env->GetFieldID(clz, name, type);
  return checkExceptionCode(env);
}

static void fini(JNIEnv *env, jclass *clazz)
{
  if (*clazz) {
    env->DeleteGlobalRef(*clazz);
    *clazz = NULL;
  }
}

static void fini(JNIEnv *env)
{
  fini(env, &runtimeExceptionClass);
  fini(env, &interruptedExceptionClass);
  fini(env, &stringClass);
  fini(env, &keyValuePairClass);
  fini(env, &channelClass);
  fini(env, &channelNanoClass);
  fini(env, &channelEasyClass);
  fini(env, &channelEasyRequestClass);
  fini(env, &channelEasyResponseClass);
}

jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
  void *tmp = NULL;
  JNIEnv *env = NULL;
  int32_t code;
  if (vm->GetEnv(&tmp, JNI_VERSION_1_2)) {
    return JNI_ERR;
  }
  env = static_cast<JNIEnv *>(tmp);

  LOG_TRACE("Loading java/lang/RuntimeException");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "java/lang/RuntimeException", &runtimeExceptionClass), error)
  LOG_TRACE("Loading java/lang/InterruptedException");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "java/lang/InterruptedException", &interruptedExceptionClass), error)
  LOG_TRACE("Loading java/lang/String");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "java/lang/String", &stringClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/KeyValuePair");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/KeyValuePair", &keyValuePairClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannel");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/impl/JniChannel", &channelClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/impl/JniChannelNano", &channelNanoClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelEasy");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/impl/JniChannelEasy", &channelEasyClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/ChannelEasy$Request");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/ChannelEasy$Request", &channelEasyRequestClass), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/ChannelEasy$Response");
  MOCHA_RPC_DO_GOTO(code, getClassGlobal(env, "com/mocha/rpc/protocol/ChannelEasy$Response", &channelEasyResponseClass), error)

  LOG_TRACE("Loading com/mocha/rpc/protocol/KeyValuePair <init> (Ljava/lang/String;Ljava/lang/String;)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", &keyValuePairConstructorMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/KeyValuePair getKey ()Ljava/lang/String;");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "getKey", "()Ljava/lang/String;", &keyValuePairGetKeyMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/KeyValuePair getValue ()Ljava/lang/String;");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "getValue", "()Ljava/lang/String;", &keyValuePairGetValueMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchRequestEvent (JI[Lcom/mocha/rpc/protocol/KeyValuePair;I)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchRequestEvent", "(JI[Lcom/mocha/rpc/protocol/KeyValuePair;I)V", &channelNanoDispatchRequestMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchResponseEvent (JI[Lcom/mocha/rpc/protocol/KeyValuePair;I)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchResponseEvent", "(JI[Lcom/mocha/rpc/protocol/KeyValuePair;I)V", &channelNanoDispatchResponseMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchPayloadEvent (JI[BZ)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchPayloadEvent", "(JI[BZ)V", &channelNanoDispatchPayloadMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchConnectedEvent ()V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchConnectedEvent", "()V", &channelNanoDispatchConnectedMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchEstablishedEvent ()V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchEstablishedEvent", "()V", &channelNanoDispatchEstablishedMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchDisconnectedEvent ()V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchDisconnectedEvent", "()V", &channelNanoDispatchDisconnectedMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano dispatchErrorEvent (ILjava/lang/String;)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelNanoClass, "dispatchErrorEvent", "(ILjava/lang/String;)V", &channelNanoDispatchErrorMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano handle, J");
  MOCHA_RPC_DO_GOTO(code, getFieldID(env, channelClass, "handle", "J", &channelHandleField), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano globalRef, J");
  MOCHA_RPC_DO_GOTO(code, getFieldID(env, channelClass, "globalRef", "J", &channelGlobalRefField), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/impl/JniChannelNano toString ([B)Ljava/lang/String;");
  MOCHA_RPC_DO_GOTO(code, getStaticMethodID(env, channelClass, "toString", "([B)Ljava/lang/String;", &channelToStringMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/ChannelEasy$Request <init> (JI[Lcom/mocha/rpc/protocol/KeyValuePair;[B)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelEasyRequestClass, "<init>", "(JI[Lcom/mocha/rpc/protocol/KeyValuePair;[B)V", &channelEasyRequestConstructorMethod), error)
  LOG_TRACE("Loading com/mocha/rpc/protocol/ChannelEasy$Response <init> (JI[Lcom/mocha/rpc/protocol/KeyValuePair;[B)V");
  MOCHA_RPC_DO_GOTO(code, getMethodID(env, channelEasyResponseClass, "<init>", "(JI[Lcom/mocha/rpc/protocol/KeyValuePair;[B)V", &channelEasyResponseConstructorMethod), error)

  pthread_key_create(&tlsContext, NULL);

  return JNI_VERSION_1_2;

error:
  LOG_ERROR("Can not initialze class/method/field descriptors");
  fini(env);
  return JNI_ERR;
}

void JNI_OnUnload(JavaVM *vm, void *reserved)
{
  void *tmp = NULL;
  JNIEnv *env = NULL;
  if (vm->GetEnv(&tmp, JNI_VERSION_1_2)) {
    return;
  }
  env = static_cast<JNIEnv *>(tmp);
  fini(env);
  pthread_key_delete(tlsContext);
}

template<typename T>
T *getChannel(jlong handle)
{
  return reinterpret_cast<T *>(handle);
}

void dispatchErrorEvent(JNIEnv *env, RPCOpaqueData eventData, jobject channel)
{
  ErrorEventData *event = static_cast<ErrorEventData *>(eventData);
  jstring message = NULL;

  convert(env, event->message, &message);
  env->CallVoidMethod(channel, channelNanoDispatchErrorMethod, event->code, message);

  if (message) {
    env->DeleteLocalRef(message);
  }

  return;
}

void dispatchPacketEvent(JNIEnv *env, int32_t eventType, RPCOpaqueData eventData, jobject channel)
{
  PacketEventData *event = static_cast<PacketEventData *>(eventData);
  jobjectArray headers = NULL;
  int32_t code = RPC_OK;
  ErrorEventData error;

  MOCHA_RPC_DO_GOTO(code, convert(env, event->headers, &headers), error)
  jmethodID method;
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_REQUEST:
      method = channelNanoDispatchRequestMethod;
      break;
    case EVENT_TYPE_CHANNEL_RESPONSE:
      method = channelNanoDispatchResponseMethod;
      break;
    default:
      code = RPC_INVALID_ARGUMENT;
      goto error;
  }
  env->CallVoidMethod(channel, method, event->id, event->code, headers, event->payloadSize);
  goto cleanupExit;

error:
  error.code = code;
  error.message = jniErrorString(code);
  LOG_ERROR("Could not dispatch packet event. %d:%s", code, error.message);
  dispatchErrorEvent(env, &error, channel);

cleanupExit:
  if (headers) {
    env->DeleteLocalRef(headers);
  }
  return;
}

void dispatchPayloadEvent(JNIEnv *env, RPCOpaqueData eventData, jobject channel)
{
  PayloadEventData *event = static_cast<PayloadEventData *>(eventData);
  jbyteArray array = NULL;
  int32_t code = RPC_OK;
  ErrorEventData error;

  MOCHA_RPC_DO_GOTO(code, convert(env, event->payload, event->size, &array), error)
  env->CallVoidMethod(channel, channelNanoDispatchPayloadMethod, event->id, event->code, array, static_cast<jboolean>(event->commit));
  goto cleanupExit;

error:
  error.code = code;
  error.message = jniErrorString(code);
  LOG_ERROR("Could not dispatch error event. %d:%s", code, error.message);
  dispatchErrorEvent(env, &error, channel);

cleanupExit:
  if (array) {
    env->DeleteLocalRef(array);
  }
  return;
}

void jniEventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  jobject jchannel = static_cast<jobject>(userData);
  TLSContext *ctx = static_cast<TLSContext *>(pthread_getspecific(tlsContext));
  JNIEnv *env = ctx->env;
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_CREATED:
      env->SetLongField(jchannel, channelHandleField, reinterpret_cast<jlong>(channel));
      break;
    case EVENT_TYPE_CHANNEL_CONNECTED:
      env->CallVoidMethod(jchannel, channelNanoDispatchConnectedMethod);
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      env->CallVoidMethod(jchannel, channelNanoDispatchEstablishedMethod);
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      env->CallVoidMethod(jchannel, channelNanoDispatchDisconnectedMethod);
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
    case EVENT_TYPE_CHANNEL_RESPONSE:
      dispatchPacketEvent(env, eventType, eventData, jchannel);
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      dispatchPayloadEvent(env, eventData, jchannel);
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      dispatchErrorEvent(env, eventData, jchannel);
    default:
      break;
  }
  checkException(env);
}

typedef int32_t (RPCChannelNano::*NanoGetAddress)(StringLite *address, uint16_t *port) const;
typedef int32_t (RPCChannelNano::*NanoGetId)(StringLite *address) const;
typedef int32_t (RPCChannelEasy::*EasyGetAddress)(StringLite *address, uint16_t *port) const;
typedef int32_t (RPCChannelEasy::*EasyGetId)(StringLite *address) const;

template<typename C, typename G>
void doGetAddress(JNIEnv *env, jlong handle, G get, jstring *address, jint *port)
{
  TlsContextHelper helper(env);
  StringLite str;
  uint16_t tmp = 0;
  int32_t code = (getChannel<C>(handle)->*get)(&str, &tmp);
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  } else {
    if (address != NULL) {
      convert(env, str.str(), address);
    }
    if (port != NULL) {
      *port = tmp;
    }
  }
}

template<typename C, typename G>
jstring doGetId(JNIEnv *env, jlong handle, G get)
{
  TlsContextHelper helper(env);
  StringLite str;
  int32_t code = (getChannel<C>(handle)->*get)(&str);
  jstring result = NULL;
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  } else {
    convert(env, str.str(), &result);
  }
  return result;
}

JNIEXPORT jstring JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetLocalAddress(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  jstring address = NULL;
  if (nano) {
    doGetAddress<RPCChannelNano, NanoGetAddress>(env, handle, &RPCChannelNano::localAddress, &address, NULL);
  } else {
    doGetAddress<RPCChannelEasy, EasyGetAddress>(env, handle, &RPCChannelEasy::localAddress, &address, NULL);
  }
  return address;
}

JNIEXPORT jint JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetLocalPort(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  jint port = 0;
  if (nano) {
    doGetAddress<RPCChannelNano, NanoGetAddress>(env, handle, &RPCChannelNano::localAddress, NULL, &port);
  } else {
    doGetAddress<RPCChannelEasy, EasyGetAddress>(env, handle, &RPCChannelEasy::localAddress, NULL, &port);
  }
  return port;
}

JNIEXPORT jstring JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetLocalId(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  if (nano) {
    return doGetId<RPCChannelNano, NanoGetId>(env, handle, &RPCChannelNano::localId);
  } else {
    return doGetId<RPCChannelEasy, EasyGetId>(env, handle, &RPCChannelEasy::localId);
  }
}

JNIEXPORT jstring JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetRemoteAddress(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  jstring address = NULL;
  if (nano) {
    doGetAddress<RPCChannelNano, NanoGetAddress>(env, handle, &RPCChannelNano::remoteAddress, &address, NULL);
  } else {
    doGetAddress<RPCChannelEasy, EasyGetAddress>(env, handle, &RPCChannelEasy::remoteAddress, &address, NULL);
  }
  return address;
}

JNIEXPORT jint JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetRemotePort(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  jint port = 0;
  if (nano) {
    doGetAddress<RPCChannelNano, NanoGetAddress>(env, handle, &RPCChannelNano::remoteAddress, NULL, &port);
  } else {
    doGetAddress<RPCChannelEasy, EasyGetAddress>(env, handle, &RPCChannelEasy::remoteAddress, NULL, &port);
  }
  return port;
}

JNIEXPORT jstring JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doGetRemoteId(JNIEnv *env, jclass clazz, jboolean nano, jlong handle)
{
  if (nano) {
    return doGetId<RPCChannelNano, NanoGetId>(env, handle, &RPCChannelNano::remoteId);
  } else {
    return doGetId<RPCChannelEasy, EasyGetId>(env, handle, &RPCChannelEasy::remoteId);
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doDestroy(JNIEnv *env, jclass clazz, jboolean nano, jobject channel, jlong handle)
{
  TlsContextHelper helper(env);
  jlong ref = env->GetLongField(channel, channelGlobalRefField);
  if (ref != INT64_MIN) {
    env->DeleteGlobalRef(reinterpret_cast<jobject>(ref));
  }
  if (nano) {
    getChannel<RPCChannelNano>(handle)->release();
  } else {
    delete getChannel<RPCChannelEasy>(handle);
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannelNano_doLoop(JNIEnv *env, jclass clazz, jlong handle, jint flags)
{
  TlsContextHelper helper(env);
  int32_t code = getChannel<RPCChannelNano>(handle)->loop(flags);
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannelNano_doBreakLoop(JNIEnv *env, jclass clazz, jlong handle)
{
  TlsContextHelper helper(env);
  int32_t code = getChannel<RPCChannelNano>(handle)->breakLoop();
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannelNano_doKeepAlive(JNIEnv *env, jclass clazz, jlong handle)
{
  TlsContextHelper helper(env);
  int32_t code = getChannel<RPCChannelNano>(handle)->keepalive();
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT jobject JNICALL Java_com_mocha_rpc_protocol_impl_JniChannelEasy_doPoll(JNIEnv *env, jclass clazz, jlong handle)
{
  TlsContextHelper helper(env);
  jobject result = NULL;
  int64_t id;
  int32_t code;
  const KeyValuePairs<StringLite, StringLite> *headers;
  const void *payload;
  size_t payloadSize;
  bool isResponse;
  int32_t st = getChannel<RPCChannelEasy>(handle)->poll(&id, &code, &headers, &payload, &payloadSize, &isResponse);
  if (MOCHA_RPC_FAILED(st)) {
    if (st == RPC_WOULDBLOCK) {
      interruptedException(env);
    } else {
      runtimeException(env, st);
    }
  } else {
    convert(env, id, code, headers, payload, payloadSize, isResponse, &result);
  }
  return result;
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannelEasy_doInterrupt(JNIEnv *env, jclass clazz, jlong handle)
{
  TlsContextHelper helper(env);
  int32_t code = getChannel<RPCChannelEasy>(handle)->interrupt();
  if (MOCHA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doResponse(JNIEnv *env, jclass clazz, jboolean nano,
    jlong handle, jlong id, jint code, jobjectArray headers, jbyteArray payload, jint offset, jint size)
{
  TlsContextHelper helper(env);
  int32_t st;
  KeyValuePairs<StringLite, StringLite> *pairs = NULL;
  KeyValuePairs<StringLite, StringLite> realPairs;
  uint8_t *rawPayload = NULL;
  if (headers) {
    MOCHA_RPC_DO_GOTO(st, convert(env, headers, &realPairs), error);
    pairs = &realPairs;
  }
  if (payload) {
    rawPayload = static_cast<uint8_t *>(env->GetPrimitiveArrayCritical(payload, NULL));
    if (rawPayload == NULL) {
      st = RPC_OUT_OF_MEMORY;
      LOG_ERROR("Running out of memory when converting payload to java byte array.");
      goto error;
    }
  }

  if (nano) {
    st = getChannel<RPCChannelNano>(handle)->response(id, code, pairs, rawPayload + offset, size);
  } else {
    st = getChannel<RPCChannelEasy>(handle)->response(id, code, pairs, rawPayload + offset, size);
  }
  if (st == RPC_OK) {
    goto cleanupExit;
  }

error:
  runtimeException(env, st);

cleanupExit:
  if (rawPayload) {
    env->ReleasePrimitiveArrayCritical(payload, rawPayload, 0);
  }
  return;
}

JNIEXPORT jlong JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doRequest(JNIEnv *env, jclass clazz, jboolean nano,
    jlong handle, jint code, jobjectArray headers, jbyteArray payload, jint offset, jint size)
{
  TlsContextHelper helper(env);
  int32_t st;
  int64_t id = 0;
  KeyValuePairs<StringLite, StringLite> *pairs = NULL;
  KeyValuePairs<StringLite, StringLite> realPairs;
  uint8_t *rawPayload = NULL;
  if (headers) {
    MOCHA_RPC_DO_GOTO(st, convert(env, headers, &realPairs), error);
    pairs = &realPairs;
  }
  if (payload) {
    rawPayload = static_cast<uint8_t *>(env->GetPrimitiveArrayCritical(payload, NULL));
    if (rawPayload == NULL) {
      st = RPC_OUT_OF_MEMORY;
      LOG_ERROR("Running out of memory when converting payload to java byte array.");
      goto error;
    }
  }

  if (nano) {
    st = getChannel<RPCChannelNano>(handle)->request(&id, code, pairs, rawPayload + offset, size);
  } else {
    st = getChannel<RPCChannelEasy>(handle)->request(&id, code, pairs, rawPayload + offset, size);
  }
  if (st == RPC_OK) {
    goto cleanupExit;
  }

error:
  runtimeException(env, st);

cleanupExit:
  if (rawPayload) {
    env->ReleasePrimitiveArrayCritical(payload, rawPayload, 0);
  }
  return id;
}

int32_t doCreateNano(const char *address, jlong timeout, jlong keepalive, jobject jchannel)
{
  RPCChannelNano::Builder *builder = RPCChannelNano::newBuilder();
  RPCChannelNano *channel = builder->connect(address)->listener(jniEventListener, jchannel)
    ->timeout(timeout)->keepalive(keepalive)->logger(rpcLogger, RPC_LOG_LEVEL_TRACE)->build();
  delete builder;
  return channel != NULL ? RPC_OK : RPC_INTERNAL_ERROR;
}

int32_t doCreateEasy(JNIEnv *env, const char *address, jlong timeout, jlong keepalive, jobject jchannel)
{
  RPCChannelEasy::Builder *builder = RPCChannelEasy::newBuilder();
  RPCChannelEasy *channel = builder->connect(address)->timeout(timeout)
    ->keepalive(keepalive)->logger(rpcLogger, RPC_LOG_LEVEL_TRACE)->build();
  delete builder;
  if (channel != NULL) {
    env->SetLongField(jchannel, channelHandleField, reinterpret_cast<jlong>(channel));
    return RPC_OK;
  } else {
    return RPC_INTERNAL_ERROR;
  }
}

JNIEXPORT void JNICALL Java_com_mocha_rpc_protocol_impl_JniChannel_doCreate
  (JNIEnv *env, jclass clazz, jboolean nano, jstring address, jlong timeout, jlong keepalive, jobject jchannel)
{
  TlsContextHelper helper(env);
  jchannel = env->NewGlobalRef(jchannel);
  env->SetLongField(jchannel, channelGlobalRefField, reinterpret_cast<jlong>(jchannel));
  const char *str = env->GetStringUTFChars(address, NULL);
  int32_t rc;

  if (nano) {
    rc = doCreateNano(str, timeout, keepalive, jchannel);
  } else {
    rc = doCreateEasy(env, str, timeout, keepalive, jchannel);
  }
  if (rc != RPC_OK) {
    env->SetLongField(jchannel, channelHandleField, INT64_MIN);
    env->SetLongField(jchannel, channelGlobalRefField, INT64_MIN);
    runtimeException(env, RPC_CAN_NOT_CONNECT);
    env->DeleteGlobalRef(jchannel);
  }
  if (str) {
    env->ReleaseStringUTFChars(address, str);
  }
}
