#include "shard_contract_interface_tests.hpp"
#include <eosio/transaction.hpp>

ACTION shard_contract_interface_tests::shard( eosio::name expected_name ) {
   eosio::print("======================shard property test========================\n");
   eosio::print( "shard name : \n", get_shard_name() );
   const auto& got_name = get_shard_name();
   check( get_shard_name()== got_name, "expected transaction shard name \"" + expected_name.to_string()
                                 + "\", but got \"" + got_name.to_string() + "\"" );
}
EOSIO_DISPATCH(shard_contract_interface_tests, (shard))

