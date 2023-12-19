#pragma once

#include <eosio/eosio.hpp>
#include <eosio/privileged.hpp>

using bytes = std::vector<char>;

class [[eosio::contract]] shard_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void regshard( uint8_t                          reg_type,
                  const eosio::registered_shard&   shard,
                  const std::optional<int64_t>&    expected_result);
};
