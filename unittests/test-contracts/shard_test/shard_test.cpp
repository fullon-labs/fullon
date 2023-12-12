#include "shard_test.hpp"
#include <eosio/privileged.hpp>

using namespace eosio;
[[eosio::action]]
void shard_test::regshard(const eosio::name& shard_name, bool enabled) {
   register_shard(shard_name, enabled);
}
