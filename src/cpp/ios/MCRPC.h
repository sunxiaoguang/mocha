#import <Foundation/Foundation.h>

@class MCRPC;

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
  MC_RPC_EVENT_TYPE_CONNECTED = 1 << 0,
  MC_RPC_EVENT_TYPE_ESTABLISHED = 1 << 1,
  MC_RPC_EVENT_TYPE_DISCONNECTED = 1 << 2,
  MC_RPC_EVENT_TYPE_REQUEST = 1 << 3,
  MC_RPC_EVENT_TYPE_RESPONSE = 1 << 4,
  MC_RPC_EVENT_TYPE_PAYLOAD = 1 << 5,
  MC_RPC_EVENT_TYPE_ERROR = 1 << 6,
  MC_RPC_EVENT_TYPE_ALL = 0xFFFFFFFF,
} MCRPCEventType;

@interface MCRPCEvent : NSObject
@property (nonatomic, weak, readonly) MCRPC * channel;
@end

@interface MCRPCPacketEvent : MCRPCEvent
@property (nonatomic, assign, readonly) int64_t id;
@property (nonatomic, assign, readonly) int32_t code;
@property (nonatomic, assign, readonly) int32_t payloadSize;
@property (nonatomic, strong, readonly) NSDictionary *headers;
@end

@interface MCRPCPayloadEvent : MCRPCEvent
@property (nonatomic, assign, readonly) int64_t id;
@property (nonatomic, assign, readonly) bool commit;
@property (nonatomic, assign, readonly) const void *payload;
@property (nonatomic, assign, readonly) int32_t payloadSize;
@end

@interface MCRPCErrorEvent : MCRPCEvent
@property (nonatomic, assign, readonly) int32_t code;
@property (nonatomic, strong, readonly) NSString *message;
@end

@protocol MCRPCEventDelegate <NSObject>
@required
- (void) onConnected:(MCRPCEvent *)event;
- (void) onEstablished:(MCRPCEvent *)event;
- (void) onDisconnected:(MCRPCEvent *)event;
- (void) onRequest:(MCRPCPacketEvent *)event;
- (void) onResponse:(MCRPCPacketEvent *)event;
- (void) onPayload:(MCRPCPayloadEvent *)event;
- (void) onError:(MCRPCErrorEvent *)event;
@end

@interface MCRPC : NSObject
@property (nonatomic, weak) id <MCRPCEventDelegate> delegate;
- (instancetype) init:(int64_t)timeout keepalive:(int64_t)keepalive flags:(int32_t)flags delegate:(id)delegate;
- (instancetype) init:(int64_t)timeout keepalive:(int64_t)keepalive delegate:(id)delegate;
- (instancetype) init:(int64_t)timeout delegate:(id)delegate;
- (instancetype) init:(id)delegate;
- (void) dealloc;
- (int32_t) connect:(NSString *)address;
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
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) response:(int64_t)id code:(int32_t)code headers:(NSDictionary *)headers;
- (int32_t) response:(int64_t)id code:(int32_t)code;
- (int32_t) response:(int64_t)id;
- (int32_t) request:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int32_t)code headers:(NSDictionary *)headers;
- (int32_t) request:(int32_t)code;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSDictionary *)headers payload:(void *)payload payloadSize:(int32_t)payloadSize;
- (int32_t) request:(int64_t *)id code:(int32_t)code headers:(NSDictionary *)headers;
- (int32_t) request:(int64_t *)id code:(int32_t)code;
@end
