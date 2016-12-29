#include "RPC.h"
#include <stdarg.h>

BEGIN_MOCA_RPC_NAMESPACE
void convert(const KeyValueMap *input, KeyValuePairs<StringLite, StringLite> *output)
{
  StringLite realKey, realValue;
  for (KeyValueMapConstIterator it = input->begin(), itend = input->end(); it != itend; ++it) {
    realKey = it->first;
    realValue = it->second;
    output->append(realKey, realValue);
  }
}

void convert(const KeyValuePairs<StringLite, StringLite> *input, KeyValueMap *output)
{
  for (size_t idx = 0, size = input->size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = input->get(idx);
    output->insert(make_pair<>(pair->key.str(), pair->value.str()));
  }
}

void RPCPacketHeaders::save(KeyValuePairs<StringLite, StringLite> *headers, ...)
{
  const char *key;
  va_list args;
  va_start(args, headers);
  StringLite tmp;
  StringLite value;
  int64_t i64;
  const string *str;
  const char *cstr;
  char buff[32];
  int32_t size;
  while ((key = va_arg(args, const char *)) != NULL) {
    tmp = key;
    switch (va_arg(args, int32_t)) {
      case INT8:
        /* FALL THROUGH */
      case INT16:
        /* FALL THROUGH */
      case INT32:
        i64 = va_arg(args, int32_t);
        goto integral;
      case INT64:
        i64 = va_arg(args, int64_t);
        goto integral;
      case FLOAT:
        /* FALL THROUGH */
      case DOUBLE:
        size = snprintf(buff, sizeof(buff), "%f", va_arg(args, double));
        goto buffer;
      case BOOLEAN:
        size = snprintf(buff, sizeof(buff), "%s", (va_arg(args, int32_t)) != 0 ? "true" : "false");
        goto buffer;
      case CSTRING:
        value.assign(va_arg(args, const char *));
        goto string;
      case STRING:
        str = va_arg(args, const string *);
        value.assign(str->data(), str->size());
        goto string;
      case STRING_LITE:
        value = *(va_arg(args, StringLite *));
        goto string;
      case OPAQUE_DATA:
        cstr = va_arg(args, const char *);
        size = va_arg(args, int32_t);
        value.assign(cstr, size);
        goto string;
      case INVALID:
        /* FALL THROUGH */
      default:
        goto exit;
    }
integral:
    size = snprintf(buff, sizeof(buff), "%lld", static_cast<long long int>(i64));
    /* FALL THROUGH */
buffer:
    value.assign(buff, size);
    /* FALL THROUGH */
string:
    headers->append(tmp, value);
    continue;
  }
exit:
  va_end(args);
}

void RPCPacketHeaders::load(const KeyValuePairs<StringLite, StringLite> *headers, ...)
{
  const char *key;
  va_list args;
  va_start(args, headers);
  union {
    void *ptr;
    int8_t *i8;
    int16_t *i16;
    int32_t *i32;
    int64_t *i64;
    float *f;
    double *d;
    bool *b;
    string *s;
    const char **cstr;
    StringLite *sl;
    struct {
      const char **data;
      int32_t *size;
    } o;
  };
  StringLite realKey;
  while ((key = va_arg(args, const char *)) != NULL) {
    realKey.assign(key);
    int32_t type = va_arg(args, int32_t);
    if (type == OPAQUE_DATA) {
      o.data = va_arg(args, const char **);
      o.size = va_arg(args, int32_t *);
    } else if (type != INVALID) {
      ptr = va_arg(args, void *);
    } else {
      goto exit;
    }
    const StringLite *v = headers->get(realKey);
    if (v) {
      switch (type) {
        case INT8:
          *i8 = strtol(v->str(), NULL, 10);
          break;
        case INT16:
          *i16 = strtol(v->str(), NULL, 10);
          break;
        case INT32:
          *i32 = strtol(v->str(), NULL, 10);
          break;
        case INT64:
          *i64 = strtoll(v->str(), NULL, 10);
          break;
        case FLOAT:
          *f = strtof(v->str(), NULL);
          break;
        case DOUBLE:
          *d = strtod(v->str(), NULL);
          break;
        case BOOLEAN:
          *b = strcasecmp("true", v->str()) == 0;
          break;
        case STRING:
          s->assign(v->str(), v->size());
          break;
        case CSTRING:
          *cstr = v->str();
          break;
        case STRING_LITE:
          *sl = *v;
          break;
        case OPAQUE_DATA:
          *o.data = v->str();
          *o.size = static_cast<int32_t>(v->size());
          break;
        default:
          break;
      }
    }
  }
exit:
  va_end(args);
}


END_MOCA_RPC_NAMESPACE
