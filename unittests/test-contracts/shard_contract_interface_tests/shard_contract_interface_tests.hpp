#include <eosio/eosio.hpp>
#include <eosio/transaction.hpp>


using namespace eosio;

CONTRACT shard_contract_interface_tests : public contract {
   public:
      using contract::contract;

      ACTION shard( eosio::name expected_name );

      using shard_action = action_wrapper<"shard"_n, &shard_contract_interface_tests::shard>;
};
