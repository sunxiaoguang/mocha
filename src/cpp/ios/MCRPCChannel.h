#import <Foundation/Foundation.h>

@class MCRPCChannelNano;
@class MCRPCChannelEasy;

typedef enum {
  MC_RPC_OK = 0,
  MC_RPC_INVALID_ARGUMENT = -1,
  MC_RPC_CORRUPTED_DATA = -2,
  MC_RPC_OUT_OF_MEMORY = -3,
  MC_RPC_CAN_NOT_CONNECT = -4,
  MC_RPC_NO_ACCESS = -5,
  MC_RPC_NOT_SUPPORTED = -6,
  MC_RPC_TOO_MANY_OPEN_FILE = -7,
  MC_RPC_INSUFFICIENT_RESOURCE = -8,
  MC_RPC_INTERNAL_ERROR = -9,
  MC_RPC_ILLEGAL_STATE = -10,
  MC_RPC_TIMEOUT = -11,
  MC_RPC_DISCONNECTED = -12,
  MC_RPC_WOULDBLOCK = -13,
  MC_RPC_INCOMPATIBLE_PROTOCOL = -14,
} MCRPCStatus;

typedef enum {
  MC_RPC_LOOP_FLAG_ONE_SHOT = 1,
  MC_RPC_LOOP_FLAG_NONBLOCK = 2,
} MCRPCLoopFlag;

typedef enum {
  MC_RPC_LOG_LEVEL_TRACE = 0,
  MC_RPC_LOG_LEVEL_DEBUG = 1,
  MC_RPC_LOG_LEVEL_INFO = 2,
  MC_RPC_LOG_LEVEL_WARN = 3,
  MC_RPC_LOG_LEVEL_ERROR = 4,
  MC_RPC_LOG_LEVEL_FATAL = 5,
  MC_RPC_LOG_LEVEL_ASSERT = 6,
} MCRPCLogLevel;

@protocol MCRPCLogger <NSObject>
@required
- (void) log:(MCRPCLogLevel)level message:(NSString *)message;
@end

@interface MCRPCDefaultLogger : NSObject<MCRPCLogger>
- (void) log:(MCRPCLogLevel)level message:(NSString *)message;
@end

@interface MCKeyValuePair : NSObject
@property (nonatomic, strong) NSString *key;
@property (nonatomic, strong) NSString *value;
- (instancetype) init:(NSString *)key value:(NSString *)value;
+ (instancetype) alloc:(NSString *)key value:(NSString *)value;
- (NSString *) description;
@end

@protocol MCRPCEventDelegate <NSObject>
@required
- (void) onConnected:(MCRPCChannelNano *)channel;
- (void) onEstablished:(MCRPCChannelNano *)channel;
- (void) onDisconnected:(MCRPCChannelNano *)channel;
- (void) onRequest:(MCRPCChannelNano *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers;
- (void) onResponse:(MCRPCChannelNano *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers;
- (void) onPayload:(MCRPCChannelNano *)channel id:(int64_t)id code:(int32_t)code commit:(bool)commit payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (void) onError:(MCRPCChannelNano *)channel code:(int32_t)code message:(NSString *)message;
@end

@interface MCRPCChannelBuilder : NSObject
- (instancetype) connect:(NSString *)address;
- (instancetype) timeout:(int64_t)timeout;
- (instancetype) keepalive:(int64_t)keepalive;
- (instancetype) limit:(int32_t)limit;
- (instancetype) listener:(id <MCRPCEventDelegate>)listener;
- (instancetype) id:(NSString *)id;
- (instancetype) logLevel:(MCRPCLogLevel)level;
- (instancetype) logger:(id <MCRPCLogger>)logger level:(MCRPCLogLevel)level;
- (MCRPCChannelNano *) nano;
- (MCRPCChannelEasy *) easy;
@end

@interface MCRPCChannelNano : NSObject
- (void) dealloc;
- (int32_t) close;
- (int32_t) localAddress:(NSString **)address port:(uint16_t *)port;
- (int32_t) localAddress:(NSString **)address;
- (int32_t) remoteAddress:(NSString **)address port:(uint16_t *) port;
- (int32_t) remoteAddress:(NSString **)address;
- (int32_t) localId:(NSString **)id;
- (int32_t) remoteId:(NSString **)id;
- (int32_t) loop:(int32_t)flags;
- (int32_t) loop;
- (int32_t) breakLoop;
- (int32_t) keepalive;
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers;
- (int32_t) response:(int64_t)id code:(int32_t)code;
- (int32_t) response:(int64_t)id;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers;
- (int32_t) request:(int32_t)code;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers;
- (int32_t) request:(int64_t *)id code:(int32_t)code;
@end

@interface MCRPCChannelEasy : NSObject
- (void) dealloc;
- (int32_t) close;
- (int32_t) localAddress:(NSString **)address port:(uint16_t *)port;
- (int32_t) localAddress:(NSString **)address;
- (int32_t) remoteAddress:(NSString **)address port:(uint16_t *) port;
- (int32_t) remoteAddress:(NSString **)address;
- (int32_t) localId:(NSString **)id;
- (int32_t) remoteId:(NSString **)id;
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSArray *)headers;
- (int32_t) response:(int64_t)id code:(int32_t)code;
- (int32_t) response:(int64_t)id;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers;
- (int32_t) request:(int32_t)code;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize
    responseCode:(int32_t *)responseCode responseHeaders:(NSArray **)responseHeaders
    responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize;
- (int32_t) request:(int32_t)code headers:(NSArray *)headers responseCode:(int32_t *)responseCode
    responseHeaders:(NSArray **)responseHeaders responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize;
- (int32_t) request:(int32_t)code responseCode:(int32_t *)responseCode responseHeaders:(NSArray **)responseHeaders
    responsePayload:(const void **)responsePayload responsePayloadSize:(int32_t *)responsePayloadSize;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSArray *)headers;
- (int32_t) request:(int64_t *)id code:(int32_t)code;
- (int32_t) poll:(int64_t *)id code:(int32_t *)code headers:(NSArray **)headers payload:(const void **)payload payloadSize:(int32_t *)payloadSize isResponse:(bool *)isResponse;
- (int32_t) interrupt;
@end
