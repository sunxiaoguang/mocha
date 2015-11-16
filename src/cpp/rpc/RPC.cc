#include "RPC.h"

BEGIN_MOCA_RPC_NAMESPACE
void convert(const KeyValueMap &input, KeyValuePairs<StringLite, StringLite> *output)
{
  for (KeyValueMapConstIterator it = input.begin(), itend = input.end(); it != itend; ++it) {
    output->append(it->first, it->second);
  }
}

void convert(const KeyValuePairs<StringLite, StringLite> &input, KeyValueMap *output)
{
  for (size_t idx = 0, size = input.size(); idx < size; ++idx) {
    const KeyValuePair<StringLite, StringLite> *pair = input.get(idx);
    output->insert(make_pair<>(pair->key.str(), pair->value.str()));
  }
}

END_MOCA_RPC_NAMESPACE
