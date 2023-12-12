#pragma once

#include <eosio/eosio.hpp>

using bytes = std::vector<char>;

class [[eosio::contract]] shard_test : public eosio::contract {
public:
   using eosio::contract::contract;

   [[eosio::action]]
   void regshard(const eosio::name& shard_name, bool enabled);
};
