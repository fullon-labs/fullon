#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#define TEST tester
using namespace eosio;
using namespace testing;
using namespace chain;

struct shard_action {
   static eosio::chain::name get_name() {
      using namespace eosio::chain::literals;
      return "shard"_n;
   }
   static eosio::chain::name get_account() {
      using namespace eosio::chain::literals;
      return "test"_n;
   }
   shard_name nm;
};
FC_REFLECT(shard_action,(nm))

BOOST_AUTO_TEST_SUITE(contract_interface_tests)

//test contract interface get_shard_name
BOOST_FIXTURE_TEST_CASE(check_shard_name_interface_test,TEST){
    create_account("test"_n);
    set_code("test"_n, eosio::testing::test_contracts::shard_contract_interface_tests_wasm());
    produce_block();
    eosio::chain::signed_transaction trx;
    trx.set_shard("main"_n);
    shard_action         shard{"main"_n};
    eosio::chain::action act(
       std::vector<eosio::chain::permission_level>{{"test"_n, eosio::chain::config::active_name}}, shard);
    trx.actions.push_back(act);
    set_transaction_headers(trx);
    auto sigs = trx.sign(get_private_key("test"_n, "active"), control->get_chain_id());
    push_transaction(trx);
    produce_block();

    produce_block();
}

BOOST_AUTO_TEST_SUITE_END()