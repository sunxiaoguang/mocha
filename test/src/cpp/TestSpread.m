#import "MCRPC.h"

@interface EventDelegate : NSObject<MCRPCEventDelegate>
@property (nonatomic, strong) NSString *remoteAddress;
@property (nonatomic, assign) uint16_t remotePort;
@property (nonatomic, strong) NSString *localAddress;
@property (nonatomic, assign) uint16_t localPort;
@property (nonatomic, strong) NSString *remoteId;
@property (nonatomic, strong) NSString *localId;
- (void) extract:(MCRPCEvent *)event;
- (void) onConnected:(MCRPCEvent *)event;
- (void) onEstablished:(MCRPCEvent *)event;
- (void) onDisconnected:(MCRPCEvent *)event;
- (void) onRequest:(MCRPCPacketEvent *)event;
- (void) onResponse:(MCRPCPacketEvent *)event;
- (void) onPayload:(MCRPCPayloadEvent *)event;
- (void) onError:(MCRPCErrorEvent *)event;
@end

@implementation EventDelegate
- (void) extract:(MCRPCEvent *)event
{
  MCRPC *channel = event.channel;
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

- (void) onConnected:(MCRPCEvent *)event
{
  [self extract:event];
  NSLog(@"Connected to server %@@%@:%hu from %@@%@:%hu", self.remoteId, self.remoteAddress, self.remotePort, self.localId, self.localAddress, self.localPort);
}

- (void) onEstablished:(MCRPCEvent *)event
{
  [self extract:event];
  NSLog(@"Session to server %@@%@:%hu from %@@%@:%hu is established", self.remoteId, self.remoteAddress, self.remotePort, self.localId, self.localAddress, self.localPort);
  [event.channel request:2 headers:@{@"s":@"t.0:t.10:t.21:l.0", @"f":@""} payload:NULL payloadSize:0];
}

- (void) onDisconnected:(MCRPCEvent *)event
{
  [self extract:event];
  NSLog(@"Disconnected from server");
}

- (void) onRequest:(MCRPCPacketEvent *)event
{
  [self extract:event];
  NSLog(@"Request %lld from server %@@%@:%hu", [event id], self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", [event code]);
  NSLog(@"Headers: %@", [event headers]);
  NSLog(@"%d bytes payload", [event payloadSize]);
  [event.channel response:[event id] code:([event code]- 100) headers:[event headers] payload:NULL payloadSize:0];
  if ([event code] == 4) {
    NSLog(@"Broadcast");
  } else {
    NSLog(@"Publish");
  }
}

- (void) onResponse:(MCRPCPacketEvent *)event
{
  [self extract:event];
  NSLog(@"Response %lld from server %@@%@:%hu", [event id], self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", [event code]);
  NSLog(@"Headers: %@", [event headers]);
  NSLog(@"%d bytes payload", [event payloadSize]);
}

- (void) onPayload:(MCRPCPayloadEvent *)event
{
  [self extract:event];
  NSLog(@"Payload of request %lld from server %@@%@:%hu", [event id], self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Size : %d", [event payloadSize]);
  NSLog(@"Commit : %@", [event commit] ? @"true" : @"false");
}

- (void) onError:(MCRPCErrorEvent *)event
{
  NSLog(@"Error %d:%@", [event code], [event message]);
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
