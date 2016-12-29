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
  StringLite k("keykeykeykeykeykeykeykeykeykeykeykeykeykeykey");
  StringLite v("valuevaluevaluevaluevaluevaluevaluevaluevalue");
  headers.append(k, v);
  int64_t id = 0;
  client->request(&id, 0, &headers);
  bool isResponse;
  int64_t responseId = 0;
  int32_t code;
  const KeyValuePairs<StringLite, StringLite> *responseHeaders = NULL;
  const void *responsePayload = NULL;
  size_t responsePayloadSize = 0;
  if (client->poll(&responseId, &code, &responseHeaders, &responsePayload, &responsePayloadSize, &isResponse)) {
    printf("caught error\n");
  } else {
    print(responseId, code, responseHeaders, responsePayload, responsePayloadSize, isResponse);
  }
  if (client->request(&id, 0, &headers, "abc", 4, &code, &responseHeaders, &responsePayload, &responsePayloadSize)) {
    printf("caught error\n");
  } else {
    print(id, code, responseHeaders, responsePayload, responsePayloadSize, isResponse);
  }
  if (argc > 2) {
    time_t start = time(NULL);
    int32_t poll = strtol(argv[2], NULL, 10);
    while ((time(NULL) - start) < poll) {
      if (client->poll(&responseId, &code, &responseHeaders, &responsePayload, &responsePayloadSize, &isResponse)) {
        printf("caught error\n");
      } else {
        print(responseId, code, responseHeaders, responsePayload, responsePayloadSize, isResponse);
      }
    }
  }
  delete client;
  return 0;
}
