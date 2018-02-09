#import "MCRPCChannel.h"
#import <mocha/rpc/RPCChannelNano.h>
#import <mocha/rpc/RPCChannelEasy.h>

using namespace mocha::rpc;

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

void rpcLogger(RPCLogLevel level, RPCOpaqueData userData, const char *func, const char *file, uint32_t line, const char *message)
{
  static char levels[] = {'T', 'D', 'I', 'W', 'E', 'F', 'A'};
  const char *tmp = strrchr(file, '/');
  if (tmp) {
    file = tmp + 1;
  }
  [(__bridge id <MCRPCLogger>) userData 
    log:(MCRPCLogLevel)level
    message:[NSString stringWithFormat:@"[%c:%s|%s:%u] %s", levels[level], func, file, line, message]];
}

void rpcEventListener(const RPCChannelNano *channelC, int32_t eventType,
    RPCOpaqueData eventData, ::mocha::rpc::RPCOpaqueData userData);
@interface MCRPCSimpleLogger : NSObject
{
  RPCSimpleLoggerSink simpleLoggerSink;
}
@property (nonatomic, weak) id <MCRPCLogger> logger;
+ (instancetype) alloc:(id <MCRPCLogger>)aLogger level:(MCRPCLogLevel)level;
- (instancetype) init:(id <MCRPCLogger>)aLogger level:(MCRPCLogLevel)level;
- (RPCOpaqueData) userData;
@end

@implementation MCRPCSimpleLogger
+ (instancetype) alloc:(id <MCRPCLogger>)aLogger level:(MCRPCLogLevel)level
{
  return [[MCRPCSimpleLogger alloc] init:aLogger level:level];
}
- (instancetype) init:(id <MCRPCLogger>)aLogger level:(MCRPCLogLevel)level
{
  simpleLoggerSink.userData = (__bridge RPCOpaqueData) aLogger;
  simpleLoggerSink.entry = rpcLogger;
  self.logger = aLogger;
  return self;
}
- (RPCOpaqueData) userData
{
  return &simpleLoggerSink;
}
@end

@implementation MCRPCDefaultLogger
- (void) log:(MCRPCLogLevel)level message:(NSString *)message
{
  NSLog(@"%@", message);
}
@end

static MCRPCDefaultLogger *defaultLogger = [MCRPCDefaultLogger alloc];

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

@interface MCRPCChannelNano()
{
  RPCChannelNano *channel;
}
@property (nonatomic, weak) id <MCRPCEventDelegate> listener;
@property (nonatomic) MCRPCSimpleLogger *logger;
@end

@implementation MCRPCChannelNano
- (void) initListener:(id<MCRPCEventDelegate>)listener
{
  self.listener = listener;
}
- (void) initLogger:(MCRPCSimpleLogger *)aLogger
{
  self.logger = aLogger;
}
- (void) init:(RPCChannelNano *)impl
{
  self->channel = impl;
}
- (int32_t) close
{
  return channel->close();
}
- (int32_t) localAddress:(NSString **)address port:(uint16_t *)port
{
  StringLite tmp;
  int32_t st = channel->localAddress(&tmp, port);
  if (!MOCHA_RPC_FAILED(st)) {
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
  int32_t st = channel->remoteAddress(&tmp, port);
  if (!MOCHA_RPC_FAILED(st)) {
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
  int32_t st = channel->localId(&tmp);
  if (!MOCHA_RPC_FAILED(st)) {
    *id = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}
- (int32_t) remoteId:(NSString **)id
{
  StringLite tmp;
  int32_t st = channel->remoteId(&tmp);
  if (!MOCHA_RPC_FAILED(st)) {
    *id = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}
- (int32_t) loop:(int32_t)flags
{
  return channel->loop(flags);
}
- (int32_t) loop
{
  return channel->loop(0);
}
- (int32_t) breakLoop
{
  return channel->breakLoop();
}
- (int32_t) keepalive
{
  return channel->keepalive();
}
- (void) dealloc
{
  if (channel != NULL) {
    channel->release();
  }
}
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return channel->response(id, code, &tmp, payload, payloadSize);
  } else {
    return channel->response(id, code, static_cast<KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
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
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
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
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return channel->request(id, code, &tmp, payload, payloadSize);
  } else {
    return channel->request(id, code, static_cast<KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
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

@interface MCRPCChannelEasy()
{
  RPCChannelEasy *channel;
}
@property (nonatomic) MCRPCSimpleLogger *logger;
@end

@implementation MCRPCChannelEasy
- (void) initLogger:(MCRPCSimpleLogger *)aLogger
{
  self.logger = aLogger;
}
- (void) init:(RPCChannelEasy *)impl
{
  self->channel = impl;
}
- (void) dealloc
{
  delete channel;
}
- (int32_t) close
{
  return channel->close();
}
- (int32_t) id:(int32_t (RPCChannelEasy::*)(StringLite *) const)method id:(NSString **)id
{
  StringLite tmp;
  int32_t st = (channel->*method)(&tmp);
  if (!MOCHA_RPC_FAILED(st)) {
    *id = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}
- (int32_t) address:(int32_t (RPCChannelEasy::*)(StringLite *, uint16_t *) const)method address:(NSString **)address port:(uint16_t *)port
{
  StringLite tmp;
  int32_t st = (channel->*method)(&tmp, port);
  if (!MOCHA_RPC_FAILED(st)) {
    *address = [NSString stringWithUTF8String:tmp.str()];
  }
  return st;
}

- (int32_t) localAddress:(NSString **)address port:(uint16_t *)port
{
  return [self address:&RPCChannelEasy::localAddress address:address port:port];
}
- (int32_t) localAddress:(NSString **)address
{
  return [self localAddress:address port:NULL];
}
- (int32_t) remoteAddress:(NSString **)address port:(uint16_t *) port
{
  return [self address:&RPCChannelEasy::remoteAddress address:address port:port];
}
- (int32_t) remoteAddress:(NSString **)address
{
  return [self remoteAddress:address port:NULL];
}
- (int32_t) localId:(NSString **)id
{
  return [self id:&RPCChannelEasy::localId id:id];
}
- (int32_t) remoteId:(NSString **)id
{
  return [self id:&RPCChannelEasy::remoteId id:id];
}
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return channel->response(id, code, &tmp, payload, payloadSize);
  } else {
    return channel->response(id, code, static_cast<KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
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
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
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
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
    responseCode:(int32_t *)responseCode responseHeaders:(NSArray **)responseHeaders
    responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize
{
  int32_t st;
  size_t size;
  const KeyValuePairs<StringLite, StringLite> *tmpHeaders;
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    st = channel->request(code, &tmp, payload, payloadSize, responseCode, &tmpHeaders, responsePayload, &size);
  } else {
    st = channel->request(code, static_cast<KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize, responseCode, &tmpHeaders, responsePayload, &size);
  }
  if (!MOCHA_RPC_FAILED(st)) {
    *responsePayloadSize = static_cast<int32_t>(size);
    *responseHeaders = convert(tmpHeaders);
  }
  return 0;
}
- (int32_t) request:(int32_t)code headers:(NSArray *)headers responseCode:(int32_t *)responseCode
    responseHeaders:(NSArray **)responseHeaders responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize
{
  return [self request:code headers:headers payload:NULL payloadSize:0 responseCode:responseCode 
    responseHeaders:responseHeaders responsePayload:responsePayload responsePayloadSize:responsePayloadSize];
}
- (int32_t) request:(int32_t)code responseCode:(int32_t *)responseCode responseHeaders:(NSArray **)responseHeaders
    responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize
{
  return [self request:code headers:NULL payload:NULL payloadSize:0 responseCode:responseCode 
    responseHeaders:responseHeaders responsePayload:responsePayload responsePayloadSize:responsePayloadSize];
}
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  if (headers) {
    KeyValuePairs<StringLite, StringLite> tmp;
    convert(headers, &tmp);
    return channel->request(id, code, &tmp, payload, payloadSize);
  } else {
    return channel->request(id, code, static_cast<KeyValuePairs<StringLite, StringLite> *>(NULL), payload, payloadSize);
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
- (int32_t) poll:(int64_t *)id code:(int32_t *)code headers:(NSArray **)headers payload:(const void **)payload payloadSize:(int32_t *)payloadSize isResponse:(bool *)isResponse
{
  int32_t st;
  const KeyValuePairs<StringLite, StringLite> *tmpHeaders;
  size_t size;
  st = channel->poll(id, code, &tmpHeaders, payload, &size, isResponse);
  if (!MOCHA_RPC_FAILED(st)) {
    *headers = convert(tmpHeaders);
  }
  return st;
}
- (int32_t) interrupt
{
  return channel->interrupt();
}
@end

@interface MCRPCChannelBuilder()
@property (nonatomic) NSString *address;
@property (nonatomic) int64_t timeout;
@property (nonatomic) int64_t keepalive;
@property (nonatomic) int32_t limit;
@property (nonatomic, weak) id <MCRPCEventDelegate> listener;
@property (nonatomic) NSString *id;
@property (nonatomic) MCRPCLogLevel logLevel;
@property (nonatomic) id <MCRPCLogger> logger;
@end

@implementation MCRPCChannelBuilder
+(instancetype) alloc
{
  MCRPCChannelBuilder *channelBuilder = [super alloc];
  if (channelBuilder) {
    channelBuilder.logger = defaultLogger;
    channelBuilder.logLevel = MC_RPC_LOG_LEVEL_INFO;
    channelBuilder.limit = 1024 * 1024;
    channelBuilder.timeout = 20 * 1000000L;
    channelBuilder.keepalive = 60 * 1000000L;
  }
  return channelBuilder;
}
- (instancetype) connect:(NSString *)address
{
  self.address = address;
  return self;
}
- (instancetype) timeout:(int64_t)timeout
{
  self.timeout = timeout;
  return self;
}
- (instancetype) keepalive:(int64_t)keepalive
{
  self.keepalive = keepalive;
  return self;
}
- (instancetype) limit:(int32_t)limit
{
  self.limit = limit;
  return self;
}
- (instancetype) listener:(id <MCRPCEventDelegate>)listener
{
  self.listener = listener;
  return self;
}
- (instancetype) id:(NSString *)id
{
  self.id = id;
  return self;
}
- (instancetype) logLevel:(MCRPCLogLevel)level
{
  return [self logger:NULL level:level];
}
- (instancetype) logger:(id <MCRPCLogger>)logger level:(MCRPCLogLevel)level
{
  self.logger = logger;
  self.logLevel = level;
  return self;
}
- (MCRPCChannelNano *) nano
{
  if (self.address == NULL || self.listener == NULL) {
    return NULL;
  }
  MCRPCChannelNano *channel = [MCRPCChannelNano alloc];
  RPCChannelNano::Builder *builder = RPCChannelNano::newBuilder();
  id <MCRPCLogger> realLogger = self.logger;
  if (realLogger == NULL) {
    realLogger = defaultLogger;
  }
  MCRPCSimpleLogger *simpleLogger = [MCRPCSimpleLogger alloc:realLogger level:self.logLevel];
  [channel initLogger:simpleLogger];
  [channel initListener:self.listener];
  builder->connect([self.address UTF8String])
    ->timeout(self.timeout)
    ->keepalive(self.keepalive)
    ->limit(self.limit)
    ->listener(rpcEventListener, (__bridge RPCOpaqueData) channel)
    ->logger(rpcSimpleLogger, (RPCLogLevel) self.logLevel, [simpleLogger userData]);
  if (self.id != NULL) {
    builder->id([self.id UTF8String]);
  }
  if (builder->build() == NULL) {
    channel = NULL;
  }
  delete builder;
  return channel;
}
- (MCRPCChannelEasy *) easy
{
  RPCChannelEasy::Builder *builder = RPCChannelEasy::newBuilder();
  id <MCRPCLogger> realLogger = self.logger;
  if (realLogger == NULL) {
    realLogger = defaultLogger;
  }
  MCRPCChannelEasy *channel = [MCRPCChannelEasy alloc];
  MCRPCSimpleLogger *simpleLogger = [MCRPCSimpleLogger alloc:realLogger level:self.logLevel];
  [channel initLogger:simpleLogger];
  builder->connect([self.address UTF8String])
    ->timeout(self.timeout)
    ->keepalive(self.keepalive)
    ->limit(self.limit)
    ->logger(rpcSimpleLogger, (RPCLogLevel) self.logLevel, [simpleLogger userData]);
  RPCChannelEasy *impl = builder->build();
  if (impl) {
    [channel init:impl];
  } else {
    channel = NULL;
  }
  delete builder;
  return channel;
}

@end

void rpcEventListener(const RPCChannelNano *channelC, int32_t eventType,
    RPCOpaqueData eventData, ::mocha::rpc::RPCOpaqueData userData)
{
  MCRPCChannelNano *channel = (__bridge MCRPCChannelNano *) userData;
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_CREATED:
      [channel performSelector:@selector(init:) withObject:(__bridge id) channelC];
      break;
    case EVENT_TYPE_CHANNEL_CONNECTED:
      if ([channel.listener respondsToSelector:@selector(onConnected:)]) {
        [channel.listener onConnected:channel];
      }
      break;
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      if ([channel.listener respondsToSelector:@selector(onEstablished:)]) {
        [channel.listener onEstablished:channel];
      }
      break;
    case EVENT_TYPE_CHANNEL_DISCONNECTED:
      if ([channel.listener respondsToSelector:@selector(onDisconnected:)]) {
        [channel.listener onDisconnected:channel];
      }
      break;
    case EVENT_TYPE_CHANNEL_REQUEST:
      if ([channel.listener respondsToSelector:@selector(onRequest: id: code: payloadSize: headers:)]) {
        PacketEventData *event = static_cast<PacketEventData *>(eventData);
        [channel.listener onRequest:channel id:event->id code:event->code payloadSize:event->payloadSize headers:convert(event->headers)];
      }
      break;
    case EVENT_TYPE_CHANNEL_RESPONSE:
      if ([channel.listener respondsToSelector:@selector(onResponse: id: code: payloadSize: headers:)]) {
        PacketEventData *event = static_cast<PacketEventData *>(eventData);
        [channel.listener onResponse:channel id:event->id code:event->code payloadSize:event->payloadSize headers:convert(event->headers)];
      }
      break;
    case EVENT_TYPE_CHANNEL_PAYLOAD:
      if ([channel.listener respondsToSelector:@selector(onPayload: id: commit: payload: payloadSize:)]) {
        PayloadEventData *event = static_cast<PayloadEventData *>(eventData);
        [channel.listener onPayload:channel id:event->id code:event->code commit:event->commit payload:event->payload payloadSize:event->size];
      }
      break;
    case EVENT_TYPE_CHANNEL_ERROR:
      if ([channel.listener respondsToSelector:@selector(onError: code: message:)]) {
        ErrorEventData *event = static_cast<ErrorEventData *>(eventData);
        [channel.listener onError:channel code:event->code message:[NSString stringWithUTF8String:event->message]];
      }
      break;
    default:
      break;
  }
}


