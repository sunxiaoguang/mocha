#import "MCRPC.h"
#import <pthread.h>
#import <unistd.h>

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
- (void) onRequest:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers;
- (void) onResponse:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers;
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
  [channel request:2 headers:@[[MCKeyValuePair alloc:@"s" value:@"t.0"], [MCKeyValuePair alloc:@"s" value:@"t."], [MCKeyValuePair alloc:@"s" value:@"t.010"],
         [MCKeyValuePair alloc:@"s" value:@"w.0"], [MCKeyValuePair alloc:@"s" value:@"w."], [MCKeyValuePair alloc:@"s" value:@"w.010"], [MCKeyValuePair alloc:@"s" value:@"cc"], 
         [MCKeyValuePair alloc:@"s" value:@"c.0"], [MCKeyValuePair alloc:@"s" value:@"c."], [MCKeyValuePair alloc:@"s" value:@"c.010"], [MCKeyValuePair alloc:@"s" value:@"cfg"], 
         [MCKeyValuePair alloc:@"s" value:@"l.0"], [MCKeyValuePair alloc:@"s" value:@"l.1"], [MCKeyValuePair alloc:@"s" value:@"l.010"]] payload:NULL payloadSize:0];
  [channel request:8 headers:@[[MCKeyValuePair alloc:@"s" value:@"ganging"]] payload:NULL payloadSize:0];
}

- (void) onDisconnected:(MCRPC *)channel
{
  [self extract:channel];
  NSLog(@"Disconnected from server");
}

- (void) onRequest:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers
{
  [self extract:channel];
  NSLog(@"Request %lld from server %@@%@:%hu", id, self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", code);
  NSLog(@"Headers: %@", headers);
  NSLog(@"%d bytes payload", payloadSize);
  //[channel response:id code:(code - 100) headers:headers payload:NULL payloadSize:0];
  if (code == 7) {
    NSLog(@"Session Push");
  } else {
    NSLog(@"Publish");
  }
}

- (void) onResponse:(MCRPC *)channel id:(int64_t)id code:(int32_t)code payloadSize:(int32_t)payloadSize headers:(NSArray *)headers
{
  [self extract:channel];
  NSLog(@"Response %lld from server %@@%@:%hu", id, self.remoteId, self.remoteAddress, self.remotePort);
  NSLog(@"Code: %d", code);
  NSLog(@"Headers: %@", headers);
  NSLog(@"%d bytes payload", payloadSize);
  //[channel request:8 headers:@[[MCKeyValuePair alloc:@"s" value:@"ganging"]] payload:NULL payloadSize:0];
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

void *thread(void *arg)
{
  EventDelegate *delegate = [[EventDelegate alloc] init];
  MCRPC *channel = [[MCRPC alloc] init:delegate];
  if (!channel) {
    NSLog(@"Could not create");
    return NULL;
  }
  if ([channel connect:[NSString stringWithUTF8String:(const char *) arg]] != 0) {
    NSLog(@"Could not connect");
    return NULL;
  }
  int32_t st;
  do {
    st = [channel loop];
  } while (st == MC_RPC_OK);
  return NULL;
}

int main(int argc, const char **argv)
{
  if (argc < 2) {
    NSLog(@"Missing required argument");
    return 1;
  }
  int numThreads = 1;
  pthread_t threads[numThreads]; 
  for (int idx = 0; idx < numThreads; ++idx) {
    pthread_create(threads + idx, NULL, thread, (void *) argv[1]);
  }

  for (int idx = 0; idx < numThreads; ++idx) {
    void *tmp;
    pthread_join(threads[idx], &tmp);
  }
  return 0;
}
