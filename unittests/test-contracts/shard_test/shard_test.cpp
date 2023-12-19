#include "shard_test.hpp"
#include <eosio/privileged.hpp>

using namespace eosio;
[[eosio::action]]
void shard_test::regshard( uint8_t                          reg_type,
                           const registered_shard&          shard,
                           const std::optional<int64_t>&    expected_result)
{
   int64_t ret = 0;
   if (reg_type == 0) {
      ret = register_shard(registered_shard_var(shard));
   } else {
      auto packed_shard = eosio::pack( std::make_tuple(
         reg_type,
         shard
      ));
      ret = internal_use_do_not_use::register_shard_packed( (char*)packed_shard.data(), packed_shard.size() );
   }
   if (expected_result) {
      check( (*expected_result) == ret, "result error! expected:" + std::to_string(*expected_result)
                                      + ", actual:" + std::to_string(ret));
   }
}
