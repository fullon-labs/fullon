#include <shard_contract_interface_tests.hpp>
#include <eosio/transaction.hpp>

ACTION shard_contract_interface_tests::shard( name nm ) {
   eosio::print("======================shard property test========================\n");
   eosio::print( "shard name : \n",get_shard_name() );
   eosio_assert( get_shard_name()==nm, "expected transaction shard name is nm" );
}
EOSIO_DISPATCH(shard_contract_interface_tests, (shard))

