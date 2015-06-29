#import "MCRPC.h"
#import "RPC.h"

using namespace moca::rpc;

@implementation MCRPCEvent
- (instancetype) init:(MCRPC *)channel
{
  self = [super init];
  if (self) {
    self->_channel = channel;
  }
  return self;
}
@end

@implementation MCRPCPacketEvent
- (instancetype) reset:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSDictionary *)headers
{
  self->_id = id;
  self->_code = code;
  self->_payloadSize = payloadSize;
  self->_headers = headers;
  return self;
}

- (instancetype) init:(MCRPC *)channel
{
  return [[super init:channel] reset:0 code:0 payloadSize:0 headers:[[NSDictionary alloc] init]];
}
@end

@implementation MCRPCPayloadEvent
- (instancetype) reset:(int64_t)id commit:(bool)commit payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  self->_id = id;
  self->_commit = commit;
  self->_payload = payload;
  self->_payloadSize = payloadSize;
  return self;
}
- (instancetype) init:(MCRPC *)channel
{
  return [[super init:channel] reset:0 commit:false payload:NULL payloadSize:0];
}
@end

@implementation MCRPCErrorEvent
- (instancetype) reset:(int32_t)code message:(NSString *)message
{
  self->_code = code;
  self->_message = message;
  return self;
}

- (instancetype) init:(MCRPC *)channel
{
  return [[super init:channel] reset:0 message:NULL];
}
@end

@interface MCRPC()
{
  RPCClient *client;
  MCRPCEvent *event;
  MCRPCPacketEvent *packetEvent;
  MCRPCPayloadEvent *payloadEvent;
  MCRPCErrorEvent *errorEvent;
}
@end

@implementation MCRPC

void convert(const NSDictionary *src, KeyValuePairs<StringLite, StringLite>* dest)
{
  for (const NSString *key in src) {
    const NSString *value = [src objectForKey:key];
    dest->append([key UTF8String], [value UTF8String]);
  }
}

NSMutableDictionary *convert(const KeyValuePairs<StringLite, StringLite>* src)
{
  NSMutableDictionary *dest = [[NSMutableDictionary alloc] init];
  for (size_t idx = 0, size = src->size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = src->get(idx);
    [dest setObject:[NSString stringWithUTF8String:pair->value.str()] forKey:[NSString stringWithUTF8String:pair->key.str()]];
  }
  return dest;
}

MCRPCPacketEvent *convert(RPCOpaqueData src, MCRPCPacketEvent *dest)
{
  PacketEventData *eventData = static_cast<PacketEventData *>(src);
  return [dest reset:eventData->id code:eventData->code payloadSize:eventData->payloadSize headers:convert(eventData->headers)];
}

MCRPCPayloadEvent *convert(RPCOpaqueData src, MCRPCPayloadEvent *dest)
{
  PayloadEventData *eventData = static_cast<PayloadEventData *>(src);
  return [dest reset:eventData->id commit:eventData->commit payload:eventData->payload payloadSize:eventData->size];
}

MCRPCErrorEvent *convert(RPCOpaqueData src, MCRPCErrorEvent *dest)
{
  ErrorEventData *eventData = static_cast<ErrorEventData *>(src);
  return [dest reset:eventData->code message:[NSString stringWithUTF8String:eventData->message.str()]];
}

void rpcEventListener(const RPCClient *client, int32_t eventType,
    RPCOpaqueData eventData, ::moca::rpc::RPCOpaqueData userData)
{
  MCRPC *channel = (__bridge MCRPC *) userData;
  switch (eventType) {
    case EVENT_TYPE_CONNECTED:
      [channel.delegate onConnected:channel->event];
      break;
    case EVENT_TYPE_ESTABLISHED:
      [channel.delegate onEstablished:channel->event];
      break;
    case EVENT_TYPE_DISCONNECTED:
      [channel.delegate onDisconnected:channel->event];
      break;
    case EVENT_TYPE_REQUEST:
      [channel.delegate onRequest:convert(eventData, channel->packetEvent)];
      break;
    case EVENT_TYPE_RESPONSE:
      [channel.delegate onResponse:convert(eventData, channel->packetEvent)];
      break;
    case EVENT_TYPE_PAYLOAD:
      [channel.delegate onPayload:convert(eventData, channel->payloadEvent)];
      break;
    case EVENT_TYPE_ERROR:
      [channel.delegate onError:convert(eventData, channel->errorEvent)];
      break;
    default:
      break;
  }
}

- (instancetype) init:(int64_t)timeout keepalive:(int64_t)keepalive flags:(int32_t)flags delegate:(id)delegate
{
  self = [super init];
  if (self) {
    self.delegate = delegate;
    self->client = RPCClient::create(timeout, keepalive, flags);
    self->event = [[MCRPCEvent alloc] init:self];
    self->packetEvent = [[MCRPCPacketEvent alloc] init:self];
    self->payloadEvent = [[MCRPCPayloadEvent alloc] init:self];
    self->errorEvent = [[MCRPCErrorEvent alloc] init:self];
    self->client->addListener(rpcEventListener, (__bridge void *) self);
  }
  return self;
}

- (instancetype) init:(int64_t)timeout keepalive:(int64_t)keepalive delegate:(id)delegate
{
  return [self init:timeout keepalive:keepalive flags:0 delegate:delegate];
}

- (instancetype) init:(int64_t)timeout delegate:(id)delegate
{
  return [self init:timeout keepalive:0x7FFFFFFFFFFFFFFFL delegate:delegate];
}

- (instancetype) init:(id)delegate
{
  return [self init:0x7FFFFFFFFFFFFFFFL delegate:delegate];
}

- (int32_t) connect:(NSString *)address
{
  return client->connect([address UTF8String]);
}

- (int32_t) close
{
  return client->close();
}

- (int32_t) localAddress:(NSString **)address port:(uint16_t *)port
{
  StringLite tmp;
  int32_t st = client->localAddress(&tmp, port);
  if (!MOCA_RPC_FAILED(st)) {
    *address = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}

- (int32_t) localAddress:(NSString **)address
{
  return [self localAddress:address port:NULL];
}

- (int32_t) remoteAddress:(NSString **)address port:(uint16_t *) port
{
  StringLite tmp;
  int32_t st = client->remoteAddress(&tmp, port);
  if (!MOCA_RPC_FAILED(st)) {
    *address = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}

- (int32_t) remoteAddress:(NSString **)address
{
  return [self remoteAddress:address port:NULL];
}

- (int32_t) localId:(NSString **)id
{
  StringLite tmp;
  int32_t st = client->localId(&tmp);
  if (!MOCA_RPC_FAILED(st)) {
    *id = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}

- (int32_t) remoteId:(NSString **)id
{
  StringLite tmp;
  int32_t st = client->remoteId(&tmp);
  if (!MOCA_RPC_FAILED(st)) {
    *id = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}

- (int32_t) loop:(int32_t)flags
{
  return client->loop(flags);
}

- (int32_t) loop
{
  return client->loop(0);
}

- (int32_t) breakLoop
{
  return client->breakLoop();
}

- (int32_t) keepalive
{
  return client->keepalive();
}

- (void) dealloc
{
  client->removeListener(rpcEventListener);
  client->release();
}

- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return client->response(id, code, &tmp, payload, payloadSize);
  } else {
    return client->response(id, code, NULL, payload, payloadSize);
  }
}

- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSDictionary *)headers
{
  return [self response:id code:code headers:headers payload:NULL payloadSize:0];
}

- (int32_t) response:(int64_t)id code:(int32_t)code
{
  return [self response:id code:code headers:NULL payload:NULL payloadSize:0];
}

- (int32_t) response:(int64_t)id
{
  return [self response:id code:0 headers:NULL payload:NULL payloadSize:0];
}

- (int32_t) request:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  int64_t discarded;
  return [self request:&discarded code:code headers:headers payload:payload payloadSize:payloadSize];
}

- (int32_t) request:(int32_t)code headers:(NSDictionary *)headers
{
  return [self request:code headers:headers payload:NULL payloadSize:0];
}

- (int32_t) request:(int32_t)code
{
  return [self request:code headers:NULL payload:NULL payloadSize:0];
}

- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return client->request(id, code, &tmp, payload, payloadSize);
  } else {
    return client->request(id, code, NULL, payload, payloadSize);
  }
}

- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSDictionary *)headers
{
  return [self request:id code:code headers:headers payload:NULL payloadSize:0];
}

- (int32_t) request:(int64_t *)id code:(int32_t)code
{
  return [self request:id code:code headers:NULL payload:NULL payloadSize:0];
}

@end
