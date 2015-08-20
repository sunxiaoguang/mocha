#include "com_moca_rpc_RPC.h"
#include "RPC.h"
#include <pthread.h>

#define RPC_JAVA_EXCEPTION (-1024)
namespace moca { namespace rpc {
JNIEnv *jniEnv;
}}

using namespace std;
using namespace moca::rpc;

struct TLSContext
{
  JNIEnv *env;
};

pthread_key_t tlsContext;

static jclass runtimeExceptionClass = NULL;
static jclass stringClass = NULL;
static jclass keyValuePairClass = NULL;
static jclass rpcClass = NULL;

static jmethodID keyValuePairConstructorMethod = NULL;
static jmethodID keyValuePairGetKeyMethod = NULL;
static jmethodID keyValuePairGetValueMethod = NULL;
static jmethodID rpcDispatchRequestMethod = NULL;
static jmethodID rpcDispatchResponseMethod = NULL;
static jmethodID rpcDispatchPayloadMethod = NULL;
static jmethodID rpcDispatchConnectedMethod = NULL;
static jmethodID rpcDispatchEstablishedMethod = NULL;
static jmethodID rpcDispatchDisconnectedMethod = NULL;
static jmethodID rpcDispatchErrorMethod = NULL;
static jmethodID rpcToStringMethod = NULL;
static jfieldID rpcHandleField = NULL;
static jfieldID rpcGlobalRefField = NULL;

static bool debugMode = false;

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
  if (MOCA_RPC_FAILED(code)){
    return code;
  }

  *str = static_cast<jstring>(env->CallStaticObjectMethod(rpcClass, rpcToStringMethod, bytes));
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
    MOCA_RPC_DO_GOTO(code, convert(env, pair->key, &key), error)
    MOCA_RPC_DO_GOTO(code, convert(env, pair->value, &value), error)
    jobject jpair = env->NewObject(keyValuePairClass, keyValuePairConstructorMethod, key, value);
    MOCA_RPC_DO_GOTO(code, checkExceptionCode(env), error)
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
      return RPC_INVALID_ARGUMENT;
    }
    jobject key = env->CallObjectMethod(pair, keyValuePairGetKeyMethod);
    if (!key) {
      return RPC_INVALID_ARGUMENT;
    }
    jobject value = env->CallObjectMethod(pair, keyValuePairGetValueMethod);
    if (!value) {
      return RPC_INVALID_ARGUMENT;
    }
    MOCA_RPC_DO(convert(env, static_cast<jstring>(key), &k));
    MOCA_RPC_DO(convert(env, static_cast<jstring>(value), &v));
    pairs->append(k, v);
  }

  return RPC_OK;
}

static void runtimeException(JNIEnv *env, int32_t code)
{
  if (MOCA_RPC_FAILED(code) && code != RPC_JAVA_EXCEPTION) {
    env->ThrowNew(runtimeExceptionClass, jniErrorString(code));
  }
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

  MOCA_RPC_DO(getClass(env, classname, &clazz))
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
    env->DeleteGlobalRef(runtimeExceptionClass);
    *clazz = NULL;
  }
}

static void fini(JNIEnv *env)
{
  fini(env, &runtimeExceptionClass);
  fini(env, &stringClass);
  fini(env, &keyValuePairClass);
  fini(env, &rpcClass);
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

  MOCA_RPC_DO_GOTO(code, getClassGlobal(env, "java/lang/RuntimeException", &runtimeExceptionClass), error)
  MOCA_RPC_DO_GOTO(code, getClassGlobal(env, "java/lang/String", &stringClass), error)
  MOCA_RPC_DO_GOTO(code, getClassGlobal(env, "com/moca/rpc/KeyValuePair", &keyValuePairClass), error)
  MOCA_RPC_DO_GOTO(code, getClassGlobal(env, "com/moca/rpc/RPC", &rpcClass), error)

  MOCA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V", &keyValuePairConstructorMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "getKey", "()Ljava/lang/String;", &keyValuePairGetKeyMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, keyValuePairClass, "getValue", "()Ljava/lang/String;", &keyValuePairGetValueMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchRequestEvent", "(JI[Lcom/moca/rpc/KeyValuePair;I)V", &rpcDispatchRequestMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchResponseEvent", "(JI[Lcom/moca/rpc/KeyValuePair;I)V", &rpcDispatchResponseMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchPayloadEvent", "(J[BZ)V", &rpcDispatchPayloadMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchConnectedEvent", "()V", &rpcDispatchConnectedMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchEstablishedEvent", "()V", &rpcDispatchEstablishedMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchDisconnectedEvent", "()V", &rpcDispatchDisconnectedMethod), error)
  MOCA_RPC_DO_GOTO(code, getMethodID(env, rpcClass, "dispatchErrorEvent", "(ILjava/lang/String;)V", &rpcDispatchErrorMethod), error)
  MOCA_RPC_DO_GOTO(code, getStaticMethodID(env, rpcClass, "toString", "([B)Ljava/lang/String;", &rpcToStringMethod), error)
  MOCA_RPC_DO_GOTO(code, getFieldID(env, rpcClass, "handle", "J", &rpcHandleField), error)
  MOCA_RPC_DO_GOTO(code, getFieldID(env, rpcClass, "globalRef", "J", &rpcGlobalRefField), error)

  pthread_key_create(&tlsContext, NULL);

  return JNI_VERSION_1_2;

error:
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

RPCClient *getClient(jlong handle)
{
  return reinterpret_cast<RPCClient *>(handle);
}

void dispatchErrorEvent(JNIEnv *env, RPCOpaqueData eventData, jobject rpc)
{
  ErrorEventData *event = static_cast<ErrorEventData *>(eventData);
  jstring message = NULL;

  convert(env, event->message, &message);
  env->CallVoidMethod(rpc, rpcDispatchErrorMethod, event->code, message);

  if (message) {
    env->DeleteLocalRef(message);
  }

  return;
}

void dispatchPacketEvent(JNIEnv *env, int32_t eventType, RPCOpaqueData eventData, jobject rpc)
{
  PacketEventData *event = static_cast<PacketEventData *>(eventData);
  jobjectArray headers = NULL;
  int32_t code = RPC_OK;
  ErrorEventData error;

  MOCA_RPC_DO_GOTO(code, convert(env, event->headers, &headers), error)
  jmethodID method;
  switch (eventType) {
    case EVENT_TYPE_REQUEST:
      method = rpcDispatchRequestMethod;
      break;
    case EVENT_TYPE_RESPONSE:
      method = rpcDispatchResponseMethod;
      break;
    default:
      code = RPC_INVALID_ARGUMENT;
      goto error;
  }
  env->CallVoidMethod(rpc, method, event->id, event->code, headers, event->payloadSize);
  goto cleanupExit;

error:
  error.code = code;
  error.message = jniErrorString(code);
  dispatchErrorEvent(env, &error, rpc);

cleanupExit:
  if (headers) {
    env->DeleteLocalRef(headers);
  }
  return;
}

void dispatchPayloadEvent(JNIEnv *env, RPCOpaqueData eventData, jobject rpc)
{
  PayloadEventData *event = static_cast<PayloadEventData *>(eventData);
  jbyteArray array;
  int32_t code = RPC_OK;
  ErrorEventData error;

  MOCA_RPC_DO_GOTO(code, convert(env, event->payload, event->size, &array), error)
  env->CallVoidMethod(rpc, rpcDispatchPayloadMethod, event->id, array, static_cast<jboolean>(event->commit));
  goto cleanupExit;

error:
  error.code = code;
  error.message = jniErrorString(code);
  dispatchErrorEvent(env, &error, rpc);

cleanupExit:
  if (array) {
    env->DeleteLocalRef(array);
  }
  return;
}

void jniEventListener(const RPCClient *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  jobject rpc = static_cast<jobject>(userData);
  TLSContext *ctx = static_cast<TLSContext *>(pthread_getspecific(tlsContext));
  JNIEnv *env = ctx->env;
  switch (eventType) {
    case EVENT_TYPE_CONNECTED:
      env->CallVoidMethod(rpc, rpcDispatchConnectedMethod);
      break;
    case EVENT_TYPE_ESTABLISHED:
      env->CallVoidMethod(rpc, rpcDispatchEstablishedMethod);
      break;
    case EVENT_TYPE_DISCONNECTED:
      env->CallVoidMethod(rpc, rpcDispatchDisconnectedMethod);
      break;
    case EVENT_TYPE_REQUEST:
    case EVENT_TYPE_RESPONSE:
      dispatchPacketEvent(env, eventType, eventData, rpc);
      break;
    case EVENT_TYPE_PAYLOAD:
      dispatchPayloadEvent(env, eventData, rpc);
      break;
    case EVENT_TYPE_ERROR:
      dispatchErrorEvent(env, eventData, rpc);
    default:
      break;
  }
  checkException(env);
}

typedef int32_t (RPCClient::*RPCGetAddress)(StringLite *address, uint16_t *port) const;
typedef int32_t (RPCClient::*RPCGetId)(StringLite *address) const;

jstring doGetAddress(JNIEnv *env, jlong handle, RPCGetAddress get)
{
  StringLite str;
  int32_t code = (getClient(handle)->*get)(&str, NULL);
  jstring result = NULL;
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  } else {
    convert(env, str.str(), &result);
  }
  return result;
}

jstring doGetId(JNIEnv *env, jlong handle, RPCGetId get)
{
  StringLite str;
  int32_t code = (getClient(handle)->*get)(&str);
  jstring result = NULL;
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  } else {
    convert(env, str.str(), &result);
  }
  return result;
}

jint doGetPort(JNIEnv *env, jlong handle, RPCGetAddress get)
{
  uint16_t port = 0;
  int32_t code = (getClient(handle)->*get)(NULL, &port);
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
  return port;
}

JNIEXPORT jstring JNICALL Java_com_moca_rpc_RPC_doGetLocalAddress(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetAddress(env, handle, &RPCClient::localAddress);
}

JNIEXPORT jint JNICALL Java_com_moca_rpc_RPC_doGetLocalPort(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetPort(env, handle, &RPCClient::localAddress);
}

JNIEXPORT jstring JNICALL Java_com_moca_rpc_RPC_doGetLocalId(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetId(env, handle, &RPCClient::localId);
}

JNIEXPORT jstring JNICALL Java_com_moca_rpc_RPC_doGetRemoteAddress(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetAddress(env, handle, &RPCClient::localAddress);
}

JNIEXPORT jint JNICALL Java_com_moca_rpc_RPC_doGetRemotePort(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetPort(env, handle, &RPCClient::localAddress);
}

JNIEXPORT jstring JNICALL Java_com_moca_rpc_RPC_doGetRemoteId(JNIEnv *env, jclass clazz, jlong handle)
{
  return doGetId(env, handle, &RPCClient::localId);
}

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doDestroy(JNIEnv *env, jclass clazz, jobject channel, jlong handle)
{
  jlong ref = env->GetLongField(channel, rpcGlobalRefField);
  if (ref != INT64_MIN) {
    env->DeleteGlobalRef(reinterpret_cast<jobject>(ref));
  }
  getClient(handle)->removeListener(jniEventListener);
  getClient(handle)->release();
}

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doLoop(JNIEnv *env, jclass clazz, jlong handle, jint flags)
{
  TLSContext ctx = {env};
  pthread_setspecific(tlsContext, &ctx);
  int32_t code = getClient(handle)->loop(flags);
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
  pthread_setspecific(tlsContext, NULL);
}

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doBreakLoop(JNIEnv *env, jclass clazz, jlong handle)
{
  int32_t code = getClient(handle)->breakLoop();
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doKeepAlive(JNIEnv *env, jclass clazz, jlong handle)
{
  int32_t code = getClient(handle)->keepalive();
  if (MOCA_RPC_FAILED(code)) {
    runtimeException(env, code);
  }
}

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doResponse(JNIEnv *env, jclass clazz, jlong handle,
    jlong id, jint code, jobjectArray headers, jbyteArray payload, jint offset, jint size)
{
  int32_t st;
  KeyValuePairs<StringLite, StringLite> *pairs = NULL;
  KeyValuePairs<StringLite, StringLite> realPairs;
  uint8_t *rawPayload = NULL;
  if (headers) {
    MOCA_RPC_DO_GOTO(st, convert(env, headers, &realPairs), error);
    pairs = &realPairs;
  }
  if (payload) {
    rawPayload = static_cast<uint8_t *>(env->GetPrimitiveArrayCritical(payload, NULL));
    if (rawPayload == NULL) {
      st = RPC_OUT_OF_MEMORY;
      goto error;
    }
  }

  if ((st = getClient(handle)->response(id, code, pairs, rawPayload + offset, size)) == RPC_OK) {
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

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doRequest(JNIEnv *env, jclass clazz, jlong handle,
    jint code, jobjectArray headers, jbyteArray payload, jint offset, jint size)
{
  int32_t st;
  KeyValuePairs<StringLite, StringLite> *pairs = NULL;
  KeyValuePairs<StringLite, StringLite> realPairs;
  uint8_t *rawPayload = NULL;
  if (headers) {
    MOCA_RPC_DO_GOTO(st, convert(env, headers, &realPairs), error);
    pairs = &realPairs;
  }
  if (payload) {
    rawPayload = static_cast<uint8_t *>(env->GetPrimitiveArrayCritical(payload, NULL));
    if (rawPayload == NULL) {
      st = RPC_OUT_OF_MEMORY;
      goto error;
    }
  }

  if ((st = getClient(handle)->request(code, pairs, rawPayload + offset, size)) == RPC_OK) {
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

JNIEXPORT void JNICALL Java_com_moca_rpc_RPC_doCreate
  (JNIEnv *env, jclass clazz, jstring address, jlong timeout, jlong keepalive, jobject rpc)
{
  TLSContext ctx = {env};
  pthread_setspecific(tlsContext, &ctx);
  rpc = env->NewGlobalRef(rpc);
  RPCClient *client = RPCClient::create(timeout, keepalive, 0);
  env->SetLongField(rpc, rpcHandleField, reinterpret_cast<jlong>(client));
  env->SetLongField(rpc, rpcGlobalRefField, reinterpret_cast<jlong>(rpc));
  int32_t code;
  const char *str = env->GetStringUTFChars(address, NULL);
  MOCA_RPC_DO_GOTO(code, client->addListener(jniEventListener, rpc), error);
  MOCA_RPC_DO_GOTO(code, client->connect(str), cleanupExit);
  goto cleanupExit;

error:
  env->SetLongField(rpc, rpcHandleField, INT64_MIN);
  env->SetLongField(rpc, rpcGlobalRefField, INT64_MIN);
  client->release();
  client = NULL;
  runtimeException(env, code);
  env->DeleteGlobalRef(rpc);

cleanupExit:
  if (str) {
    env->ReleaseStringUTFChars(address, str);
  }
  pthread_setspecific(tlsContext, NULL);
}
