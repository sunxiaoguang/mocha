#include <moca/rpc/RPCChannelEasy.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

using namespace moca::rpc;

int main(int argc, char **argv)
{
  auto builder = RPCChannelEasy::newBuilder();
  auto client = builder->connect(argv[1])->keepalive(1000000)->build();
  delete builder;
  if (!client) {
    return 1;
  }
  KeyValuePairs<StringLite, StringLite> headers;
  StringLite k("key");
  StringLite v("value");
  headers.append(k, v);
  int64_t id = 0;
  int32_t code;
  const KeyValuePairs<StringLite, StringLite> *responseHeaders = NULL;
  const void *responsePayload = NULL;
  size_t responsePayloadSize = 0;
  time_t start = time(NULL);
  int total = 1000000;
  for (int idx = 0; idx < total; ++idx) {
    if (client->request(&id, 0, &headers, NULL, 0, &code, &responseHeaders, &responsePayload, &responsePayloadSize)) {
      printf("caught error\n");
      exit(1);
    }
    if (idx % 100000 == 99999) {
      printf("%d elapsed time %lld\n", idx + 1, (long long)(time(NULL) - start));
    }
  }
  time_t end = time(NULL);
  printf("Throughput %dps\nLatency %dus\n", (int)(total / (end - start)), (int)(end - start) * 1000000 / total);
  delete client;
  return 0;
}
