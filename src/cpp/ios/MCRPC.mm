#import "MCRPC.h"
#import "RPCClient.h"

using namespace moca::rpc;

@implementation MCKeyValuePair
- (instancetype) init:(NSString *)key value:(NSString *)value
{
  self = [super init];
  if (self) {
    self.key = key;
    self.value = value;
  }
  return self;
}
+ (instancetype) alloc:(NSString *)key value:(NSString *)value
{
  return [[MCKeyValuePair alloc] init:key value:value];
}
- (NSString *) description
{
  return [NSString stringWithFormat:@"(%@ => %@)", self.key, self.value];
}
@end

@interface MCRPC()
{
  RPCClient *client;
}
@end

@implementation MCRPC

void convert(const NSArray *src, KeyValuePairs<StringLite, StringLite>* dest)
{
  for (const MCKeyValuePair *pair in src) {
    dest->append([pair.key UTF8String], [pair.value UTF8String]);
  }
}

NSMutableArray *convert(const KeyValuePairs<StringLite, StringLite>* src)
{
  NSMutableArray *dest = [[NSMutableArray alloc] init];
  for (size_t idx = 0, size = src->size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = src->get(idx);
    [dest addObject:[[MCKeyValuePair alloc] 
      init:[NSString stringWithUTF8String:pair->key.str()] 
      value:[NSString stringWithUTF8String:pair->value.str()]]];
  }
  return dest;
}

void rpcEventListener(const RPCClient *client, int32_t eventType,
    RPCOpaqueData eventData, ::moca::rpc::RPCOpaqueData userData)
{
  MCRPC *channel = (__bridge MCRPC *) userData;
  switch (eventType) {
    case EVENT_TYPE_CONNECTED:
      if ([channel.delegate respondsToSelector:@selector(onConnected:)]) {
        [channel.delegate onConnected:channel];
      }
      break;
    case EVENT_TYPE_ESTABLISHED:
      if ([channel.delegate respondsToSelector:@selector(onEstablished:)]) {
        [channel.delegate onEstablished:channel];
      }
      break;
    case EVENT_TYPE_DISCONNECTED:
      if ([channel.delegate respondsToSelector:@selector(onDisconnected:)]) {
        [channel.delegate onDisconnected:channel];
      }
      break;
    case EVENT_TYPE_REQUEST:
      if ([channel.delegate respondsToSelector:@selector(onRequest: id: code: payloadSize: headers:)]) {
        PacketEventData *event = static_cast<PacketEventData *>(eventData);
        [channel.delegate onRequest:channel id:event->id code:event->code payloadSize:event->payloadSize headers:convert(event->headers)];
      }
      break;
    case EVENT_TYPE_RESPONSE:
      if ([channel.delegate respondsToSelector:@selector(onResponse: id: code: payloadSize: headers:)]) {
        PacketEventData *event = static_cast<PacketEventData *>(eventData);
        [channel.delegate onResponse:channel id:event->id code:event->code payloadSize:event->payloadSize headers:convert(event->headers)];
      }
      break;
    case EVENT_TYPE_PAYLOAD:
      if ([channel.delegate respondsToSelector:@selector(onPayload: id: commit: payload: payloadSize:)]) {
        PayloadEventData *event = static_cast<PayloadEventData *>(eventData);
        [channel.delegate onPayload:channel id:event->id commit:event->commit payload:event->payload payloadSize:event->size];
      }
      break;
    case EVENT_TYPE_ERROR:
      if ([channel.delegate respondsToSelector:@selector(onError: code: message:)]) {
        ErrorEventData *event = static_cast<ErrorEventData *>(eventData);
        [channel.delegate onError:channel code:event->code message:[NSString stringWithUTF8String:event->message]];
      }
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

- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return client->response(id, code, &tmp, payload, payloadSize);
  } else {
    return client->response(id, code, NULL, payload, payloadSize);
  }
}

- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers
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

- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  int64_t discarded;
  return [self request:&discarded code:code headers:headers payload:payload payloadSize:payloadSize];
}

- (int32_t) request:(int32_t)code headers:(NSArray *)headers
{
  return [self request:code headers:headers payload:NULL payloadSize:0];
}

- (int32_t) request:(int32_t)code
{
  return [self request:code headers:NULL payload:NULL payloadSize:0];
}

- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return client->request(id, code, &tmp, payload, payloadSize);
  } else {
    return client->request(id, code, NULL, payload, payloadSize);
  }
}

- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers
{
  return [self request:id code:code headers:headers payload:NULL payloadSize:0];
}

- (int32_t) request:(int64_t *)id code:(int32_t)code
{
  return [self request:id code:code headers:NULL payload:NULL payloadSize:0];
}

@end
