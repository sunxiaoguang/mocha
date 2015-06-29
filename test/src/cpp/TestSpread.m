#import "MCRPC.h"

@interface EventDelegate : NSObject<MCRPCEventDelegate>
@property (nonatomic, strong) NSString *remoteAddress;
@property (nonatomic, assign) uint16_t remotePort;
@property (nonatomic, strong) NSString *localAddress;
@property (nonatomic, assign) uint16_t localPort;
@property (nonatomic, strong) NSString *remoteId;
@property (nonatomic, strong) NSString *localId;
- (void) extract:(MCRPC *)channel;
- (void) onConnected:(MCRPC *)channel;
- (void) onEstablished:(MCRPC *)channel;
- (void) onDisconnected:(MCRPC *)channel;
- (void) onRequest:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSDictionary *)headers;
- (void) onResponse:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSDictionary *)headers;
- (void) onPayload:(MCRPC *)channel id:(int64_t)id commit:(bool)commit payload:(const void *)payload payloadSize:(int32_t)payloadSize;
- (void) onError:(MCRPC *)channel code:(int32_t)code message:(NSString *)message;
@end

@implementation EventDelegate
- (void) extract:(MCRPC *)channel
{
  NSString *address = NULL;
  uint16_t port;
  [channel remoteAddress:&address port:&port];
  self.remoteAddress = address;
  self.remotePort = port;
  [channel localAddress:&address port:&port];
  self.localAddress = address;
  self.localPort = port;
  NSString *id = NULL;
  [channel remoteId:&id];
  self.remoteId = id;
  [channel localId:&id];
  self.localId = id;
}

- (void) onConnected:(MCRPC *)channel
{
  [self extract:channel];
  NSLog(@"Connected to server %@@%@:%hu from %@@%@:%hu", self.remoteId, self.remoteAddress, self.remotePort, self.localId, self.localAddress, self.localPort);
}

- (void) onEstablished:(MCRPC *)channel
{
  [self extract:channel];
  NSLog(@"Session to server %@@%@:%hu from %@@%@:%hu is established", self.remoteId, self.remoteAddress, self.remotePort, self.localId, self.localAddress, self.localPort);
  [channel request:2 headers:@{@"s":@"t.0:t.10:t.21:l.0", @"f":@""} payload:NULL payloadSize:0];
}

- (void) onDisconnected:(MCRPC *)channel
{
  [self extract:channel];
  NSLog(@"Disconnected from server");
}

- (void) onRequest:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSDictionary *)headers
{
  [self extract:channel];
  NSLog(@"Request %lld from server %@@%@:%hu", id, self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", code);
  NSLog(@"Headers: %@", headers);
  NSLog(@"%d bytes payload", payloadSize);
  [channel response:id code:(code - 100) headers:headers payload:NULL payloadSize:0];
  if (code == 4) {
    NSLog(@"Broadcast");
  } else {
    NSLog(@"Publish");
  }
}

- (void) onResponse:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSDictionary *)headers
{
  [self extract:channel];
  NSLog(@"Response %lld from server %@@%@:%hu", id, self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", code);
  NSLog(@"Headers: %@", headers);
  NSLog(@"%d bytes payload", payloadSize);
}

- (void) onPayload:(MCRPC *)channel id:(int64_t)id commit:(bool)commit payload:(const void *)payload payloadSize:(int32_t)payloadSize
{
  [self extract:channel];
  NSLog(@"Payload of request %lld from server %@@%@:%hu", id, self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Size : %d", payloadSize);
  NSLog(@"Commit : %@", commit ? @"true" : @"false");
}

- (void) onError:(MCRPC *)channel code:(int32_t)code message:(NSString *)message;
{
  NSLog(@"Error %d:%@", code, message);
}

@end

int main(int argc, const char **argv)
{
  if (argc < 2) {
    NSLog(@"Missing required argument");
    return 1;
  }
  EventDelegate *delegate = [[EventDelegate alloc] init];
  MCRPC *channel = [[MCRPC alloc] init:delegate];
  if (!channel) {
    NSLog(@"Could not create");
    return 1;
  }
  if ([channel connect:[NSString stringWithUTF8String:argv[1]]] != 0) {
    NSLog(@"Could not connect");
    return 1;
  }
  int32_t st;
  do {
    st = [channel loop];
  } while (st == MC_RPC_OK);
  return 0;
}
