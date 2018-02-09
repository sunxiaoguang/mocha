#include <Python.h>
#include <mocha/rpc/RPCChannelEasy.h>
#include <structmember.h>
#include <memory>
#include <limits.h>
#include <map>

using namespace mocha::rpc;
using namespace std;

#define CHECK_AND_INCREF(X) do { if ((X) != NULL) { Py_INCREF((X)); } } while (0);
#define CHECK_AND_DECREF(X) do { if ((X) != NULL) { Py_DECREF((X)); (X) = NULL; } } while (0);

typedef int32_t (*Callable)(RPCOpaqueData userData);

/* workaround stupid python c api const correctness issue */
template<typename T>
static T *makeMutable(const T *t)
{
  return const_cast<T *>(t);
}
template<typename T>
Callable callable(T *any)
{
  return reinterpret_cast<Callable>(any);
}

struct RPCPacket
{
  int64_t id;
  int32_t code;
  int32_t flags;
  KeyValuePairs<StringLite, StringLite> *headers;
  StringLite *payload;
  union {
    KeyValuePairs<StringLite, StringLite> *realHeaders;
    const KeyValuePairs<StringLite, StringLite> *realHeadersConst;
  };
  union {
    RPCOpaqueData realPayload;
    const void *realPayloadConst;
  };
  size_t realPayloadSize;
  KeyValuePairs<StringLite, StringLite> backingHeaders;
  StringLite backingPayload;
};

/* Channel related stuff */
struct PyChannelEasy {
  PyObject_HEAD
  StringLite address;
  StringLite id;
  int64_t timeout;
  int64_t keepalive;
  int32_t limit;
  int32_t retry;
  uint16_t localPort;
  uint16_t remotePort;
  StringLite localAddress;
  StringLite remoteAddress;
  StringLite localId;
  StringLite remoteId;
  RPCChannelEasy *channel;
  RPCPacket request;
  RPCPacket response;
};

static PyObject *channelEasyNew(PyTypeObject *type, PyObject *args, PyObject *kwds);
static int channelEasyInit(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static void channelEasyDealloc(PyChannelEasy *self);
static PyObject *channelEasyRequest(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyRequestPayload(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyRequestHeaders(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyRequestPing(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyResponse(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyResponsePayload(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyResponseHeaders(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyResponsePong(PyChannelEasy *self, PyObject *args, PyObject *kwds);
static PyObject *channelEasyClose(PyChannelEasy *self);
static PyObject *channelEasyPoll(PyChannelEasy *self);
static PyObject *channelEasyInterrupt(PyChannelEasy *self);
static PyObject *channelEasyGetLocalAddress(PyChannelEasy *self);
static PyObject *channelEasyGetRemoteAddress(PyChannelEasy *self);
static PyObject *channelEasyGetLocalId(PyChannelEasy *self);
static PyObject *channelEasyGetRemoteId(PyChannelEasy *self);
static int channelEasyReadOnly(PyChannelEasy *self, PyObject *value, void *closure);

static PyMethodDef channelEasyMethods[] = {
  {"request", (PyCFunction)channelEasyRequest,
    METH_VARARGS | METH_KEYWORDS, "send out request"
  },
  {"request_payload", (PyCFunction)channelEasyRequestPayload,
    METH_VARARGS | METH_KEYWORDS, "send out request with payload only"
  },
  {"request_headers", (PyCFunction)channelEasyRequestHeaders,
    METH_VARARGS | METH_KEYWORDS, "send out request with headers only"
  },
  {"ping", (PyCFunction)channelEasyRequestPing,
    METH_VARARGS | METH_KEYWORDS, "send out ping request with code only"
  },
  {"response", (PyCFunction)channelEasyResponse,
    METH_VARARGS | METH_KEYWORDS, "send out response in non blocking way"
  },
  {"response_payload", (PyCFunction)channelEasyResponsePayload,
    METH_VARARGS | METH_KEYWORDS, "send out response with payload only"
  },
  {"response_headers", (PyCFunction)channelEasyResponseHeaders,
    METH_VARARGS | METH_KEYWORDS, "send out response with response only"
  },
  {"pong", (PyCFunction)channelEasyResponsePong,
    METH_VARARGS | METH_KEYWORDS, "send out pong response with code only"
  },
  {"close", (PyCFunction)channelEasyClose,
    METH_NOARGS, "close the channel intentionally, will be opened again during the next call"
  },
  {"response_payload", (PyCFunction)channelEasyResponsePayload,
    METH_VARARGS | METH_KEYWORDS, "send out response with payload only"
  },
  {"response_headers", (PyCFunction)channelEasyResponseHeaders,
    METH_VARARGS | METH_KEYWORDS, "send out response with headers only"
  },
  {"poll", (PyCFunction)channelEasyPoll,
    METH_NOARGS, "poll for request or response in blocking way"
  },
  {"interrupt", (PyCFunction)channelEasyInterrupt, METH_NOARGS,
    "send out request and wait for response in blocking way"
  },
  {NULL}
};
static PyMemberDef channelEasyMembers[] = {
  {NULL}
};
static PyGetSetDef channelEasyGetSetters[] = {
  {makeMutable("local_address"), (getter) channelEasyGetLocalAddress, (setter) channelEasyReadOnly, makeMutable("local address"), NULL},
  {makeMutable("remote_address"), (getter) channelEasyGetRemoteAddress, (setter) channelEasyReadOnly, makeMutable("remote address"), NULL},
  {makeMutable("local_id"), (getter) channelEasyGetLocalId, (setter) channelEasyReadOnly, makeMutable("local id"), NULL},
  {makeMutable("remote_id"), (getter) channelEasyGetRemoteId, (setter) channelEasyReadOnly, makeMutable("remote id"), NULL},
  {NULL},
};


static PyTypeObject channelEasyType = {
  PyObject_HEAD_INIT(NULL)
  0,                                /* ob_size */
  "mocharpc.ChannelEasy",            /* tp_name */
  sizeof(PyChannelEasy),            /* tp_basicsize */
  0,                                /* tp_itemsize */
  (destructor) channelEasyDealloc,  /* tp_dealloc */
  0,                                /* tp_print */
  0,                                /* tp_getattr */
  0,                                /* tp_setattr */
  0,                                /* tp_compare */
  0,                                /* tp_repr */
  0,                                /* tp_as_number */
  0,                                /* tp_as_sequence */
  0,                                /* tp_as_mapping */
  0,                                /* tp_hash */
  0,                                /* tp_call */
  0,                                /* tp_str */
  0,                                /* tp_getattro */
  0,                                /* tp_setattro */
  0,                                /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,               /* tp_flags */
  "Easy Channel",                   /* tp_doc */
  0,                                /* tp_traverse */
  0,                                /* tp_clear */
  0,                                /* tp_richcompare */
  0,                                /* tp_weaklistoffset */
  0,                                /* tp_iter */
  0,                                /* tp_iternext */
  channelEasyMethods,               /* tp_methods */
  channelEasyMembers,               /* tp_members */
  channelEasyGetSetters,            /* tp_getset */
  0,                                /* tp_base */
  0,                                /* tp_dict */
  0,                                /* tp_descr_get */
  0,                                /* tp_descr_set */
  0,                                /* tp_dictoffset */
  (initproc)channelEasyInit,        /* tp_init */
  0,                                /* tp_alloc */
  channelEasyNew,                   /* tp_new */
};

enum PyPacketFlags {
  PACKET_IS_RESPONSE = 1 << 0,
  PACKET_IS_READONLY = 1 << 1,
};

/* Packet related stuff */
struct PyPacket {
  PyObject_HEAD
  RPCPacket packet;
};

static PyObject *packetNew(PyTypeObject *type, PyObject *args, PyObject *kwds);
static int packetInit(PyPacket *self, PyObject *args, PyObject *kwds);
static void packetDealloc(PyPacket *self);
static PyObject *packetGetId(PyPacket *self, void *closure);
static int packetSetId(PyPacket *self, PyObject *value, void *closure);
static PyObject *packetGetCode(PyPacket *self, void *closure);
static int packetSetCode(PyPacket *self, PyObject *value, void *closure);
static PyObject *packetGetHeaders(PyPacket *self, void *closure);
static int packetSetHeaders(PyPacket *self, PyObject *value, void *closure);
static PyObject *packetGetPayload(PyPacket *self, void *closure);
static int packetSetPayload(PyPacket *self, PyObject *value, void *closure);

static PyMethodDef packetMethods[] = {
  {NULL}
};

#ifdef T_LONGLONG
#define T_INT64 T_LONGLONG
#else
#define T_INT64 T_LONG
#endif

static PyMemberDef packetMembers[] = {
  {NULL}
};

static PyGetSetDef packetGetSetters[] = {
  {makeMutable("id"), (getter) packetGetId, (setter) packetSetId, makeMutable("id"), NULL},
  {makeMutable("code"), (getter) packetGetCode, (setter) packetSetCode, makeMutable("code"), NULL},
  {makeMutable("headers"), (getter) packetGetHeaders, (setter) packetSetHeaders, makeMutable("headers"), NULL},
  {makeMutable("payload"), (getter) packetGetPayload, (setter) packetSetPayload, makeMutable("payload"), NULL},
  {NULL},
};


static PyTypeObject requestType = {
  PyObject_HEAD_INIT(NULL)
  0,                                /* ob_size */
  "mocharpc.Request",                /* tp_name */
  sizeof(PyPacket),                 /* tp_basicsize */
  0,                                /* tp_itemsize */
  (destructor) packetDealloc,       /* tp_dealloc */
  0,                                /* tp_print */
  0,                                /* tp_getattr */
  0,                                /* tp_setattr */
  0,                                /* tp_compare */
  0,                                /* tp_repr */
  0,                                /* tp_as_number */
  0,                                /* tp_as_sequence */
  0,                                /* tp_as_mapping */
  0,                                /* tp_hash */
  0,                                /* tp_call */
  0,                                /* tp_str */
  0,                                /* tp_getattro */
  0,                                /* tp_setattro */
  0,                                /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,               /* tp_flags */
  "Request Packet",                 /* tp_doc */
  0,                                /* tp_traverse */
  0,                                /* tp_clear */
  0,                                /* tp_richcompare */
  0,                                /* tp_weaklistoffset */
  0,                                /* tp_iter */
  0,                                /* tp_iternext */
  packetMethods,                    /* tp_methods */
  packetMembers,                    /* tp_members */
  packetGetSetters,                 /* tp_getset */
  0,                                /* tp_base */
  0,                                /* tp_dict */
  0,                                /* tp_descr_get */
  0,                                /* tp_descr_set */
  0,                                /* tp_dictoffset */
  (initproc)packetInit,             /* tp_init */
  0,                                /* tp_alloc */
  packetNew,                        /* tp_new */
};

static PyTypeObject responseType = {
  PyObject_HEAD_INIT(NULL)
  0,                                /* ob_size */
  "mocharpc.Response",               /* tp_name */
  sizeof(PyPacket),                 /* tp_basicsize */
  0,                                /* tp_itemsize */
  (destructor) packetDealloc,       /* tp_dealloc */
  0,                                /* tp_print */
  0,                                /* tp_getattr */
  0,                                /* tp_setattr */
  0,                                /* tp_compare */
  0,                                /* tp_repr */
  0,                                /* tp_as_number */
  0,                                /* tp_as_sequence */
  0,                                /* tp_as_mapping */
  0,                                /* tp_hash */
  0,                                /* tp_call */
  0,                                /* tp_str */
  0,                                /* tp_getattro */
  0,                                /* tp_setattro */
  0,                                /* tp_as_buffer */
  Py_TPFLAGS_DEFAULT,               /* tp_flags */
  "Response Packet",                /* tp_doc */
  0,                                /* tp_traverse */
  0,                                /* tp_clear */
  0,                                /* tp_richcompare */
  0,                                /* tp_weaklistoffset */
  0,                                /* tp_iter */
  0,                                /* tp_iternext */
  packetMethods,                    /* tp_methods */
  packetMembers,                    /* tp_members */
  packetGetSetters,                 /* tp_getset */
  0,                                /* tp_base */
  0,                                /* tp_dict */
  0,                                /* tp_descr_get */
  0,                                /* tp_descr_set */
  0,                                /* tp_dictoffset */
  (initproc)packetInit,             /* tp_init */
  0,                                /* tp_alloc */
  packetNew,                        /* tp_new */
};

static PyMethodDef methods[] = {
  {NULL}
};

static map<int32_t, PyObject *> rpcErrorMapping;

extern "C" {

void initError(void)
{
  rpcErrorMapping[RPC_OK] = NULL;
  rpcErrorMapping[RPC_INVALID_ARGUMENT] = PyExc_Exception;
  rpcErrorMapping[RPC_CORRUPTED_DATA] = PyExc_IOError;
  rpcErrorMapping[RPC_OUT_OF_MEMORY] = PyExc_MemoryError;
  rpcErrorMapping[RPC_CAN_NOT_CONNECT] = PyExc_IOError;
  rpcErrorMapping[RPC_NO_ACCESS] = PyExc_SystemError;
  rpcErrorMapping[RPC_NOT_SUPPORTED] = PyExc_NotImplementedError;
  rpcErrorMapping[RPC_TOO_MANY_OPEN_FILE] = PyExc_OSError;
  rpcErrorMapping[RPC_INSUFFICIENT_RESOURCE] = PyExc_OSError;
  rpcErrorMapping[RPC_INTERNAL_ERROR] = PyExc_Exception;
  rpcErrorMapping[RPC_ILLEGAL_STATE] = PyExc_Exception;
  rpcErrorMapping[RPC_TIMEOUT] = PyExc_Exception;
  rpcErrorMapping[RPC_DISCONNECTED] = PyExc_EOFError;
  rpcErrorMapping[RPC_WOULDBLOCK] = PyExc_Exception;
  rpcErrorMapping[RPC_INCOMPATIBLE_PROTOCOL] = PyExc_Exception;
  rpcErrorMapping[RPC_CAN_NOT_BIND] = PyExc_EnvironmentError;
  rpcErrorMapping[RPC_BUFFER_OVERFLOW] = PyExc_OverflowError;
  rpcErrorMapping[RPC_CANCELED] = PyExc_Exception;
}

PyMODINIT_FUNC
initmocharpc(void)
{
  if (PyType_Ready(&channelEasyType) < 0 ||
      PyType_Ready(&requestType) < 0 ||
      PyType_Ready(&responseType) < 0) {
    return;
  }
  PyObject *mod;
  if ((mod = Py_InitModule3("mocharpc", methods, "MochaRPC module.")) == NULL) {
    return;
  }

  initError();
  Py_INCREF(&channelEasyType);
  PyModule_AddObject(mod, "ChannelEasy", (PyObject *)&channelEasyType);
}
}

/* error handling */
static int handleRPCError(int32_t st, const char *message = NULL)
{
  PyObject *error = rpcErrorMapping.count(st) > 0 ? rpcErrorMapping[st] : NULL;
  if (error != NULL || MOCHA_RPC_FAILED(st)) {
    char buff[32];
    snprintf(buff, sizeof(buff), "%d", st);
    string msg(message == NULL ? "Error" : message);
    msg.append(". ");
    msg.append(buff);
    msg.append(":");
    msg.append(errorString(st));
    PyErr_SetString(error == NULL ? PyExc_Exception : error, msg.c_str());
  }
  return st;
}

static int handleTypeErrorV(const PyTypeObject *actual, va_list args)
{
  string message("Expecting ");
  const PyTypeObject *expecting;
  int32_t index = 0;
  while ((expecting = va_arg(args, const PyTypeObject *)) != NULL) {
    if (index++ > 0) {
      message.append(" or ");
    }
    message.append(expecting->tp_name);
  }
  va_end(args);
  if (index == 1) {
    message.append(" ");
  }
  message.append("when ");
  message.append(actual->tp_name);
  message.append(" is given");
  PyErr_SetString(PyExc_TypeError, message.c_str());
  return -1;
}

static int32_t handleTypeError(const PyObject *object, ...)
{
  va_list args;
  va_start(args, object);
  handleTypeErrorV(Py_TYPE(object), args);
  va_end(args);
  return -1;
}

static int handleReadonlyError()
{
  PyErr_SetString(PyExc_RuntimeError, "Modifying readonly variable");
  return -1;
}

static int handleCanNotDeleteError(const char *attribute)
{
  string message("Cannot delete attribute '");
  message.append(attribute);
  message.append("'");
  PyErr_SetString(PyExc_TypeError, message.c_str());
  return -1;
}

static int handleInvalidArgumentError(const char *error = NULL)
{
  string message("Invalid argument");
  if (error != NULL) {
    message.append(":");
    message.append(error);
  }
  PyErr_SetString(PyExc_TypeError, message.c_str());
  return -1;
}

/* type conversions */
static inline int32_t convert(PyObject *from, int32_t *to)
{
  if (!PyInt_Check(from) && !PyLong_Check(from)) {
    return handleTypeError(from, &PyInt_Type, &PyLong_Type);
  }

  *to = PyInt_Check(from) ? PyInt_AsLong(from) : PyLong_AsLong(from);
  return RPC_OK;
}

static inline int32_t convert(PyObject *from, int64_t *to)
{
  if (!PyLong_Check(from) || !PyInt_Check(from)) {
    return handleTypeError(from, &PyInt_Type, &PyLong_Type);
  }
  *to = PyLong_Check(from) ? PyLong_AsLong(from) : PyInt_AsLong(from);
  return RPC_OK;
}

static inline PyObject *convert(RPCOpaqueData from, size_t size)
{
  return PyBytes_FromStringAndSize(static_cast<char *>(from), size);
}
static inline PyObject *convert(const StringLite *from)
{
  return PyString_FromStringAndSize(from->str(), from->size());
}

static inline void convertUnsafe(PyObject *from, StringLite *to)
{
  if (PyString_Check(from)) {
    to->assign(PyString_AsString(from), PyString_Size(from));
  } else {
    to->assign(PyByteArray_AsString(from), PyByteArray_Size(from));
  }
}

static inline int32_t convert(PyObject *from, StringLite *to)
{
  if (!PyString_Check(from) && !PyByteArray_Check(from)) {
    return handleTypeError(from, &PyString_Type, &PyByteArray_Type);
  }
  convertUnsafe(from, to);
  return RPC_OK;
}

static PyObject *convert(const KeyValuePairs<StringLite, StringLite> *from)
{
  PyObject *to = PyList_New(0);
  for (Py_ssize_t idx = 0, size = from->size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = from->get(idx);
    PyObject *tuple = PyTuple_New(2);
    PyTuple_SetItem(tuple, 0, convert(&pair->key));
    PyTuple_SetItem(tuple, 1, convert(&pair->value));
    PyList_Append(to, tuple);
  }
  return to;
}

static int32_t convertUnsafe(PyObject *from, KeyValuePairs<StringLite, StringLite> *to)
{
  StringLite key, value;
  for (Py_ssize_t idx = 0, size = PyList_Size(from); idx < size; ++idx) {
    PyObject *item = PyList_GET_ITEM(from, idx);
    if (!PyTuple_Check(item)) {
      return handleTypeError(item, &PyTuple_Type);
    }
    if (PyTuple_Size(item) < 2) {
      return handleInvalidArgumentError("header tuple has less than two items");
    }
    convert(PyTuple_GET_ITEM(item, 0), &key);
    convert(PyTuple_GET_ITEM(item, 1), &value);
    to->append(key, value);
    key.shrink(0);
    value.shrink(0);
  }
  return RPC_OK;
}

static inline int32_t convert(PyObject *from, KeyValuePairs<StringLite, StringLite> *to)
{
  if (!PyList_Check(from)) {
    return handleTypeError(from, &PyList_Type);
  }
  convertUnsafe(from, to);
  return RPC_OK;
}

/* execution */
static int32_t withRetry(int32_t retry, int64_t deadline, Callable callback, RPCOpaqueData userData)
{
  int32_t st;
retry:
  if (MOCHA_RPC_FAILED(st = callback(userData))) {
    if (--retry >= 0) {
      goto retry;
    }
    return st;
  }
  return st;
}

/* bits */
static RPCPacket *loadHeaders(RPCPacket *packet, const KeyValuePairs<StringLite, StringLite> *headers)
{
  if (packet->headers != NULL) {
    packet->headers->clear();
  } else if (headers != NULL) {
    new (&packet->backingHeaders) KeyValuePairs<StringLite, StringLite>;
    packet->headers = &packet->backingHeaders;
  }

  if (headers != NULL) {
    /* TODO avoid this extra copy later on */
    *packet->headers = *headers;
    packet->realHeaders = packet->headers;
  } else {
    packet->realHeaders = NULL;
  }
  return packet;
}

static RPCPacket *loadPayload(RPCPacket *packet, const void *payload, size_t payloadSize)
{
  bool hasPayload = payload != NULL && payloadSize > 0;
  if (packet->payload != NULL) {
    packet->payload->shrink(0);
  } else if (hasPayload) {
    new (&packet->backingPayload) StringLite;
    packet->payload = &packet->backingPayload;
  }

  if (hasPayload) {
    packet->payload->assign(static_cast<const char *>(payload), payloadSize);
    packet->realPayloadConst = packet->payload->str();
    packet->realPayloadSize = packet->payload->size();
  } else {
    packet->realPayload = NULL;
    packet->realPayloadSize = 0;
  }
  return packet;
}

static RPCPacket *loadPacket(RPCPacket *packet, int64_t id, int32_t code,
    const KeyValuePairs<StringLite, StringLite> *headers,
    const void *payload, size_t payloadSize)
{
  packet->id = id;
  packet->code = code;
  loadPayload(loadHeaders(packet, headers), payload, payloadSize);
  return packet;
}

static inline PyPacket *loadPacket(PyPacket *packet, int64_t id, int32_t code,
    const KeyValuePairs<StringLite, StringLite> *headers,
    const void *payload, size_t payloadSize)
{
  loadPacket(&packet->packet, id, code, headers, payload, payloadSize);
    return packet;
}

static inline PyObject *loadPacket(PyObject *packet, int64_t id, int32_t code,
    const KeyValuePairs<StringLite, StringLite> *headers,
    const void *payload, size_t payloadSize)
{
  return reinterpret_cast<PyObject *>(loadPacket(reinterpret_cast<PyPacket *>(packet),
        id, code, headers, payload, payloadSize));
}

static RPCPacket *loadPacket(RPCPacket *packet, int64_t id, int32_t code,
    PyObject *headers, Py_buffer *payload)
{
  KeyValuePairs<StringLite, StringLite> tmp;
  KeyValuePairs<StringLite, StringLite> *realHeaders = NULL;
  RPCOpaqueData realPayload = NULL;
  size_t realPayloadSize = 0;
  if (headers) {
    if (convert(headers, &tmp) != 0) {
      return NULL;
    }
    realHeaders = &tmp;
  }
  if (payload && payload->len > 0) {
    realPayload = payload->buf;
    realPayloadSize = payload->len;
  }
  return loadPacket(packet, id, code, realHeaders, realPayload, realPayloadSize);
}

static inline PyPacket *loadPacket(PyPacket *packet, int64_t id, int32_t code,
    PyObject *headers, Py_buffer *payload)
{
  if (loadPacket(&packet->packet, id, code, headers, payload) != NULL) {
    return packet;
  } else {
    return NULL;
  }
}

static inline PyObject *loadPacket(PyObject *packet, int64_t id, int32_t code,
    PyObject *headers, Py_buffer *payload)
{
  return reinterpret_cast<PyObject *>(loadPacket(reinterpret_cast<PyPacket *>(packet),
        id, code, headers, payload));
}

static inline PyObject *createPacket(int64_t id, int32_t code,
    const KeyValuePairs<StringLite, StringLite> *headers,
    const void *payload, size_t payloadSize, bool isResponse)
{
  return loadPacket(packetNew(isResponse ? &responseType : &requestType, NULL, NULL),
      id, code, headers, payload, payloadSize);
}

static bool packetIsReadonly(PyPacket *self)
{
  return (self->packet.flags & PACKET_IS_READONLY) == PACKET_IS_READONLY;
}

static PyObject *packetGetId(PyPacket *self, void *closure)
{
  return Py_BuildValue("L", self->packet.id);
}

static int packetSetId(PyPacket *self, PyObject *value, void *closure)
{
  if (packetIsReadonly(self)) {
    return handleReadonlyError();
  }
  if (value == NULL) {
    return handleCanNotDeleteError("id");
  }

  return convert(value, &self->packet.id);
}

static PyObject *packetGetCode(PyPacket *self, void *closure)
{
  return Py_BuildValue("i", self->packet.code);
}

static int packetSetCode(PyPacket *self, PyObject *value, void *closure)
{
  if (packetIsReadonly(self)) {
    return handleReadonlyError();
  }
  if (value == NULL) {
    return handleCanNotDeleteError("code");
  }
  return convert(value, &self->packet.code);
}

static PyObject *packetGetHeaders(PyPacket *self, void *closure)
{
  return convert(self->packet.realHeaders);
}

static int packetSetHeaders(PyPacket *self, PyObject *value, void *closure)
{
  if (packetIsReadonly(self)) {
    return handleReadonlyError();
  }
  if (value == NULL) {
    return handleCanNotDeleteError("headers");
  }
  KeyValuePairs<StringLite, StringLite> headers;
  MOCHA_RPC_DO(convert(value, &headers));
  loadHeaders(&self->packet, &headers);
  return RPC_OK;
}

static PyObject *packetGetPayload(PyPacket *self, void *closure)
{
  return convert(self->packet.realPayload, self->packet.realPayloadSize);
}

static int packetSetPayload(PyPacket *self, PyObject *value, void *closure)
{
  if (packetIsReadonly(self)) {
    return handleReadonlyError();
  }
  if (value == NULL) {
    return handleCanNotDeleteError("payload");
  }

  StringLite payload;
  MOCHA_RPC_DO(convert(value, &payload));
  loadPayload(&self->packet, payload.str(), payload.size());
  return RPC_OK;
}

static void packetDealloc(RPCPacket *packet)
{
  if (packet->headers) {
    packet->backingHeaders.~KeyValuePairs<StringLite, StringLite>();
  }
  if (packet->payload) {
    packet->backingPayload.~StringLite();
  }
}

static int channelEasyReadOnly(PyChannelEasy *self, PyObject *value, void *closure)
{
  return handleReadonlyError();
}

static int32_t channelEasyDoDisconnect(PyChannelEasy *self)
{
  if (self->channel != NULL) {
    self->channel->close();
    delete self->channel;
    self->channel = NULL;
    self->remoteAddress.assign("0.0.0.0");
    self->localAddress.assign("0.0.0.0");
    self->remoteId.shrink(0);
    self->localId.shrink(0);
    self->remotePort = self->localPort = 0;
  }
  return RPC_OK;
}

static int32_t channelEasyHandleRPCError(PyChannelEasy *self, int32_t st)
{
  channelEasyDoDisconnect(self);
  if (!PyErr_Occurred()) {
    handleRPCError(st);
  }
  return -1;
}

int32_t channelEasyAfterEstablished(PyChannelEasy *self)
{
  int32_t st;
  if (MOCHA_RPC_FAILED(st = self->channel->localId(&self->localId)) ||
      MOCHA_RPC_FAILED(st = self->channel->localAddress(&self->localAddress, &self->localPort)) ||
      MOCHA_RPC_FAILED(st = self->channel->remoteAddress(&self->remoteAddress, &self->remotePort)) ||
      MOCHA_RPC_FAILED(st = self->channel->remoteId(&self->remoteId))) {
    return channelEasyHandleRPCError(self, st);
  }
  return RPC_OK;
}

static void dummyRPCLogger(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *fmt, ...)
{
}

static int32_t channelEasyDoConnect(PyChannelEasy *self)
{
  auto_ptr<RPCChannelEasy::Builder> builder(RPCChannelEasy::newBuilder());
  if (self->id.size() > 0) {
    builder->id(self->id);
  }
  builder->logger(dummyRPCLogger, RPC_LOG_LEVEL_ASSERT);
  self->channel = builder->timeout(self->timeout)->keepalive(self->keepalive)->
    limit(self->limit)->connect(self->address)->build();
  int32_t st = RPC_CAN_NOT_CONNECT;
  if (self->channel == NULL || MOCHA_RPC_FAILED(channelEasyAfterEstablished(self))) {
    st = channelEasyHandleRPCError(self, st);
  }
  return st;
}

static RPCChannelEasy *channelEasyGetChannel(PyChannelEasy *self)
{
  if (self->channel == NULL) {
    withRetry(2, self->timeout, callable(channelEasyDoConnect), self);
  }
  return self->channel;
}

static int32_t channelEasyDoRequest(PyChannelEasy *self)
{
  RPCChannelEasy *channel;
  if ((channel = channelEasyGetChannel(self)) == NULL) {
    return RPC_CAN_NOT_CONNECT;
  }
  return channel->request(&self->request.id, self->request.code,
        self->request.realHeaders, self->request.realPayload, self->request.realPayloadSize);
}

static int32_t channelEasyDoRequestBlocking(PyChannelEasy *self)
{
  RPCChannelEasy *channel;
  if ((channel = channelEasyGetChannel(self)) == NULL) {
    return RPC_CAN_NOT_CONNECT;
  }
  return channel->request(&self->request.id, self->request.code,
      self->request.realHeaders, self->request.realPayload, self->request.realPayloadSize,
      &self->response.code, &self->response.realHeadersConst,
      &self->response.realPayloadConst, &self->response.realPayloadSize);
}

static PyObject *
channelEasyRequestImpl(PyChannelEasy *self, int32_t code, PyObject *headers, Py_buffer *payload, PyObject *blocking)
{
  loadPacket(&self->request, INT64_MIN, code, headers, payload);
  int32_t st;
  RPCPacket *packet;
  bool isResponse;
  if (blocking && PyObject_IsTrue(blocking)) {
    st = withRetry(self->retry, self->timeout, callable(channelEasyDoRequestBlocking), self);
    packet = &self->response;
    isResponse = true;
  } else {
    st = withRetry(self->retry, self->timeout, callable(channelEasyDoRequest), self);
    packet = &self->request;
    isResponse = false;
  }
  if (MOCHA_RPC_FAILED(st)) {
    channelEasyHandleRPCError(self, st);
    return NULL;
  } else {
    return createPacket(packet->id, packet->code, packet->realHeaders,
        packet->realPayload, packet->realPayloadSize, isResponse);
  }
}

static PyObject *
channelEasyRequestPayload(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  Py_buffer payload = {0, NULL};
  PyObject *blocking = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"code", "payload", "blocking", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|s*O!", const_cast<char **>(kwlist),
        &self->request.code, &payload, &PyBool_Type, &blocking)) {
    return NULL;
  }

  return channelEasyRequestImpl(self, self->request.code, NULL, &payload, blocking);
}

static PyObject *
channelEasyRequestHeaders(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  PyObject *headers = NULL, *blocking = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"code", "headers", "blocking", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|O!O!", const_cast<char **>(kwlist),
        &self->request.code, &PyList_Type, &headers, &PyBool_Type, &blocking)) {
    return NULL;
  }

  return channelEasyRequestImpl(self, self->request.code, headers, NULL, blocking);
}

static PyObject *
channelEasyRequestPing(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  PyObject *blocking = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"code", "blocking", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|O!", const_cast<char **>(kwlist),
        &self->request.code, &PyBool_Type, &blocking)) {
    return NULL;
  }

  return channelEasyRequestImpl(self, self->request.code, NULL, NULL, blocking);
}

static PyObject *
channelEasyRequest(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  Py_buffer payload = {0, NULL};
  PyObject *headers = NULL, *blocking = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"code", "headers", "payload", "blocking", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|O!s*O!", const_cast<char **>(kwlist),
        &self->request.code, &PyList_Type, &headers, &payload, &PyBool_Type, &blocking)) {
    return NULL;
  }

  return channelEasyRequestImpl(self, self->request.code, headers, &payload, blocking);
}

static int32_t channelEasyDoResponse(PyChannelEasy *self)
{
  RPCChannelEasy *channel;
  if ((channel = channelEasyGetChannel(self)) == NULL) {
    return RPC_CAN_NOT_CONNECT;
  }
  return channel->response(self->response.id, self->response.code,
        self->response.realHeaders, self->response.realPayload, self->response.realPayloadSize);
}

static PyObject *
channelEasyResponseImpl(PyChannelEasy *self, int64_t id, int32_t code, PyObject *headers, Py_buffer *payload)
{
  loadPacket(&self->response, id, code, headers, payload);
  int32_t st;
  if (MOCHA_RPC_FAILED(st = withRetry(self->retry, self->timeout, callable(channelEasyDoResponse), self))) {
    channelEasyHandleRPCError(self, st);
    return NULL;
  } else {
    return createPacket(self->response.id, self->response.code, self->response.realHeaders,
      self->response.realPayload, self->response.realPayloadSize, true);
  }
}

static PyObject *
channelEasyResponse(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  Py_buffer payload = {0, NULL};
  PyObject *headers = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"id", "code", "headers", "payload", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Li|O!s*", const_cast<char **>(kwlist),
        &self->response.id, &self->response.code, &PyList_Type, &headers, &payload)) {
    return NULL;
  }

  return channelEasyResponseImpl(self, self->response.id, self->response.code, headers, &payload);
}

static PyObject *
channelEasyResponsePayload(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  Py_buffer payload = {0, NULL};
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"id", "code", "payload", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Li|s*", const_cast<char **>(kwlist),
        &self->response.id, &self->response.code, &payload)) {
    return NULL;
  }

  return channelEasyResponseImpl(self, self->response.id, self->response.code, NULL, &payload);
}

static PyObject *
channelEasyResponseHeaders(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  PyObject *headers = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"id", "code", "headers", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Li|O!", const_cast<char **>(kwlist),
        &self->response.id, &self->response.code, &PyList_Type, &headers)) {
    return NULL;
  }

  return channelEasyResponseImpl(self, self->response.id, self->response.code, headers, NULL);
}

static PyObject *
channelEasyResponsePong(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"id", "code", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "Li", const_cast<char **>(kwlist),
        &self->response.id, &self->response.code)) {
    return NULL;
  }

  return channelEasyResponseImpl(self, self->response.id, self->response.code, NULL, NULL);
}

static PyObject *
channelEasyClose(PyChannelEasy *self)
{
  channelEasyDoDisconnect(self);
  return Py_None;
}

static PyObject *
channelEasyPoll(PyChannelEasy *self)
{
  int64_t id;
  int32_t code;
  const KeyValuePairs<StringLite, StringLite> *headers;
  const void *payload;
  size_t payloadSize;
  bool isResponse;
  int32_t st = self->channel->poll(&id, &code, &headers, &payload, &payloadSize, &isResponse);
  if (MOCHA_RPC_FAILED(st)) {
    channelEasyHandleRPCError(self, st);
    return NULL;
  } else {
    return createPacket(id, code, headers, payload, payloadSize, isResponse);
  }
}

static int
channelEasyInit(PyChannelEasy *self, PyObject *args, PyObject *kwds)
{
  const char *address = NULL, *id = "";
  self->address.assign("127.0.0.1:1234");
  self->id.shrink(0);
  self->keepalive = self->timeout = 0x7FFFFFFFFFFFFFFFL;
  self->limit = 0x7FFFFFFFL;
  self->retry = 2;
  channelEasyDoDisconnect(self);
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"address", "id", "timeout", "keepalive", "limit", "retry", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|zLLLiO", const_cast<char **>(kwlist),
        &address, &id, &self->timeout, &self->keepalive, &self->limit, &self->retry)) {
    return -1;
  }
  self->id.assign(id);
  self->address.assign(address);
  if (channelEasyGetChannel(self) == NULL) {
    return channelEasyHandleRPCError(self, RPC_CAN_NOT_CONNECT);
  } else {
    return RPC_OK;
  }
}

static void
channelEasyDealloc(PyChannelEasy *self)
{
  self->address.~StringLite();
  self->id.~StringLite();
  self->remoteAddress.~StringLite();
  self->localAddress.~StringLite();
  self->remoteId.~StringLite();
  self->localId.~StringLite();
  packetDealloc(&self->request);
  packetDealloc(&self->response);
  self->ob_type->tp_free(reinterpret_cast<PyObject *>(self));
}

static PyObject *
channelEasyNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  PyChannelEasy *self;
  self = (PyChannelEasy *) type->tp_alloc(type, 0);
  if (self != NULL) {
    new (&self->address) StringLite("127.0.0.1:1234");
    new (&self->id) StringLite;
    new (&self->remoteAddress) StringLite("0.0.0.0");
    new (&self->localAddress) StringLite("0.0.0.0");
    new (&self->remoteId) StringLite;
    new (&self->localId) StringLite;
    self->remotePort = self->localPort = 0;
    self->keepalive = self->timeout = 0x7FFFFFFFFFFFFFFFL;
    self->limit = 0x7FFFFFFFL;
    self->retry = 2;
  }
  return (PyObject *) self;
}

static PyObject *channelEasyInterrupt(PyChannelEasy *self)
{
  return Py_BuildValue("i", self->channel->interrupt());
}

static inline int32_t channelEasyCheckConnected(PyChannelEasy *self)
{
  if (self->channel == NULL) {
    return handleRPCError(MOCHA_RPC_DISCONNECTED, "Channel is not connected yet");
  } else {
    return RPC_OK;
  }
}

static PyObject *channelEasyGetId(PyChannelEasy *self, const StringLite *id)
{
  if (MOCHA_RPC_FAILED(channelEasyCheckConnected(self))) {
    return NULL;
  }
  return Py_BuildValue("s", id->str());
}

static PyObject *channelEasyGetAddress(PyChannelEasy *self, const StringLite *address, uint16_t port)
{
  if (MOCHA_RPC_FAILED(channelEasyCheckConnected(self))) {
    return NULL;
  }
  return Py_BuildValue("sH", address->str(), port);
}

static PyObject *channelEasyGetLocalAddress(PyChannelEasy *self)
{
  return channelEasyGetAddress(self, &self->localAddress, self->localPort);
}

static PyObject *channelEasyGetRemoteAddress(PyChannelEasy *self)
{
  return channelEasyGetAddress(self, &self->remoteAddress, self->remotePort);
}

static PyObject *channelEasyGetLocalId(PyChannelEasy *self)
{
  return channelEasyGetId(self, &self->localId);
}

static PyObject *channelEasyGetRemoteId(PyChannelEasy *self)
{
  return channelEasyGetId(self, &self->remoteId);
}

static PyObject *packetNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  PyPacket *self = (PyPacket *) type->tp_alloc(type, 0);
  if (self != NULL) {
    self->packet.flags |= (type == &responseType ? PACKET_IS_RESPONSE : 0);
    self->packet.id = INT64_MIN;
  }
  return (PyObject *) self;
}

static int packetInit(PyPacket *self, PyObject *args, PyObject *kwds)
{
  Py_buffer payload = {0, NULL};
  PyObject *headers = NULL;
  /* supress warning, stupid python c interface, it's actually never written */
  static const char *kwlist[] = {"code", "headers", "payload", NULL};
  if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|O!s*", const_cast<char **>(kwlist),
        &self->packet.code, &PyList_Type, &headers, &payload)) {
    return -1;
  }
  loadPacket(self, INT64_MIN, self->packet.code, headers, &payload);
  PyBuffer_Release(&payload);
  return RPC_OK;
}

static void packetDealloc(PyPacket *self)
{
  packetDealloc(&self->packet);
  self->ob_type->tp_free(reinterpret_cast<PyObject *>(self));
}

