#define __STDC_FORMAT_MACROS
#include <mocha/rpc/RPCChannelNano.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

using namespace mocha::rpc;

#ifdef __DARWIN__
#define INT64_FMT "%lld"
#else
#define INT64_FMT "%ld"
#endif

RPCChannelNano *client;

int32_t total = 1000000;
volatile int32_t finished = 0;
int32_t sending = 0;
time_t startTime;
time_t endTime;

KeyValuePairs<StringLite, StringLite> headers;
void fire(const RPCChannelNano *channel)
{
  if (sending >= total) {
    return;
  }
  for (int32_t startTime = sending, endTime = sending + 100; startTime < endTime; ++startTime) {
    client->request(0, &headers);
  }
  sending += 100;
}

void eventListener(const RPCChannelNano *channel, int32_t eventType, RPCOpaqueData eventData, RPCOpaqueData userData)
{
  switch (eventType) {
    case EVENT_TYPE_CHANNEL_RESPONSE:
      if (finished++ % 100000 == 99999) {
        printf("%d elapsed time %lld\n", finished, (long long)(time(NULL) - startTime));
      }
      if (finished == total) {
        endTime = time(NULL);
        channel->breakLoop();
      }
      /* FALL THROUGH */
    case EVENT_TYPE_CHANNEL_ESTABLISHED:
      fire(channel);
      break;
  }
}

int main(int argc, char **argv)
{
  auto builder = RPCChannelNano::newBuilder();
  client = builder->connect(argv[1])->listener(eventListener, NULL, EVENT_TYPE_CHANNEL_RESPONSE | EVENT_TYPE_CHANNEL_ESTABLISHED)->timeout(1000000000l)->keepalive(1000000)->build();
  if (!client) {
    printf("Could not create\n");
    return 1;
  }

  StringLite k("key");
  StringLite v("value");
  headers.append(k, v);
  startTime = time(NULL);
  while (finished < total) {
    client->loop();
  }
  printf("Throughput %dps\nLatency %dus\n", (int)(total / (endTime - startTime)), (int)(endTime - startTime) * 1000000 / total);
  return 0;
}
