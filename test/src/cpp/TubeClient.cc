#define __STDC_FORMAT_MACROS
#include <moca/rpc/RPCChannelEasy.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

using namespace moca::rpc;

void print(int64_t id, int32_t code, const KeyValuePairs<StringLite, StringLite> *headers, const void *payload, size_t payloadSize, bool response)
{
  printf("received %s %" PRId64 " with code %d\n", response ? "response" : "request", id, code);
  if (headers) {
    printf("%zd headers\n", headers->size());
    for (size_t idx = 0, size = headers->size(); idx < size; ++idx) {
      const KeyValuePair<StringLite, StringLite> *pair = headers->get(idx);
      printf("header[%zd]: '%s' => '%s'\n", idx, pair->key.str(), pair->value.str());
    }
  }
  if (payload && payloadSize > 0) {
    printf("%zd bytes payload: %s\n", payloadSize, static_cast<const char *>(payload));
  }
}

int main(int argc, char **argv)
{
  auto builder = RPCChannelEasy::newBuilder();
  auto client = builder->connect(argv[1])->keepalive(1000000)->build();
  delete builder;
  if (!client) {
    return 1;
  }
  KeyValuePairs<StringLite, StringLite> headers;
  StringLite ku("ui");
  StringLite vu("F69AA91C");
  StringLite kk("k");
  StringLite vk(argv[2]);
  StringLite kf("f");
  StringLite vf("2");
  headers.append(ku, vu);
  headers.append(kk, vk);
  headers.append(kf, vf);
  int64_t id = 0;
  client->request(&id, 1, &headers);
  bool isResponse;
  int64_t responseId = 0;
  int32_t code;
  const KeyValuePairs<StringLite, StringLite> *responseHeaders = NULL;
  const void *responsePayload = NULL;
  size_t responsePayloadSize = 0;
  if (argc > 3) {
    time_t start = time(NULL);
    int32_t poll = strtol(argv[3], NULL, 10);
    int32_t st = 0;
    while ((time(NULL) - start) < poll && st == 0) {
      if ((st = client->poll(&responseId, &code, &responseHeaders, &responsePayload, &responsePayloadSize, &isResponse))) {
        printf("caught error %d\n", st);
      } else {
        print(responseId, code, responseHeaders, responsePayload, responsePayloadSize, isResponse);
      }
    }
  }
  delete client;
  return 0;
}
