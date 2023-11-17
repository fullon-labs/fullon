#include <eosio/eosio.hpp>

extern "C" {
__attribute__((eosio_wasm_import))
    void  eosio_assert( uint32_t test, const char* msg );

__attribute__((eosio_wasm_import))
    eosio::name get_shard_name();
}
using namespace eosio;

CONTRACT shard_contract_interface_tests : public contract {
   public:
      using contract::contract;

      ACTION shard( eosio::name expected_name );

      using shard_action = action_wrapper<"shard"_n, &shard_contract_interface_tests::shard>;
};
