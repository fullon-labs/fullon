#include <eosio/eosio.hpp>
using namespace eosio;
extern "C" {
__attribute__((eosio_wasm_import))
    void  eosio_assert( uint32_t test, const char* msg );

__attribute__((eosio_wasm_import))
    name get_shard_name();
}

CONTRACT shard_contract_interface_tests : public contract {
   public:
      using contract::contract;

      ACTION shard( name nm );

      using shard_action = action_wrapper<"shard"_n, &shard_contract_interface_tests::shard>;
};
