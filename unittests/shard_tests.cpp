#include <eosio/chain/config.hpp>
#include <eosio/chain/shard_object.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#define TEST tester
using namespace eosio;
using namespace testing;
using namespace chain;

using mvo = fc::mutable_variant_object;

class shard_base_tester : public TEST {
public:
   static constexpr name contract_name   = "shard.test"_n;
   static constexpr name shard1_name     = "sub.shard1"_n;
   static constexpr name shard1_owner    = "owner.shard1"_n;

   shard_base_tester() {
      produce_blocks();

      create_accounts( { contract_name, shard1_owner, "alice"_n, "bob"_n, "carol"_n } );
      produce_blocks();

      set_code( contract_name, test_contracts::shard_test_wasm() );
      set_abi( contract_name, test_contracts::shard_test_abi().data() );

      produce_blocks();

      const auto& accnt = control->db().get<account_object,by_name>( contract_name );
      abi_def abi;
      BOOST_REQUIRE_EQUAL(abi_serializer::to_abi(accnt.abi, abi), true);
      abi_ser.set_abi(std::move(abi), abi_serializer::create_yield_function( abi_serializer_max_time ));
   }

   transaction_trace_ptr push_action(const action& act, const account_name& signer)
   { try {
      signed_transaction trx;
      trx.actions.emplace_back(act);
      if (signer) {
         trx.actions.back().authorization = vector<permission_level>{{signer, config::active_name}};
      }
      set_transaction_headers(trx);
      if (signer) {
         trx.sign(get_private_key(signer, "active"), control->get_chain_id());
      }
      return push_transaction(trx);

   } FC_CAPTURE_AND_RETHROW( (act)(signer) ) }

   transaction_trace_ptr push_action( const account_name& signer, const action_name &name, const variant_object &data ) {
      string action_type_name = abi_ser.get_action_type(name);

      action act;
      act.account = contract_name;
      act.name    = name;
      act.data    = abi_ser.variant_to_binary( action_type_name, data, abi_serializer::create_yield_function( abi_serializer_max_time ) );

      return push_action( std::move(act), signer );
   }

   auto regshard( const account_name&           signer,
                  uint8_t                       reg_type,
                  const account_name&           name,
                  uint8_t                       shard_type,
                  const account_name&           owner,
                  bool                          enabled,
                  uint8_t                       opts,
                  const std::optional<int64_t>& expected_result) {

      return push_action( signer, "regshard"_n, mvo()
           ( "reg_type",         reg_type)
           ( "shard",            mvo()
               ( "name",             name)
               ( "shard_type",       shard_type)
               ( "owner",            owner)
               ( "enabled",          enabled)
               ( "opts",             opts)
           )
           ( "expected_result",  expected_result)
      );
   }

   auto regshard(const account_name& signer, const registered_shard &shard, const std::optional<int64_t>& expected_result) {
      return regshard(signer, 0, shard.name, uint8_t(shard.shard_type), shard.owner, shard.enabled, shard.opts, expected_result);
   }

   abi_serializer abi_ser;
};

#define REQUIRE_MATCHING_REGISTER_SHARD(reg_shard, shard_obj) { \
      REQUIRE_EQUAL_VARIANT( (reg_shard).name,        (shard_obj).name ); \
      REQUIRE_EQUAL_VARIANT( (reg_shard).shard_type,  (shard_obj).shard_type ); \
      REQUIRE_EQUAL_VARIANT( (reg_shard).owner,       (shard_obj).owner ); \
      BOOST_REQUIRE_EQUAL(   (reg_shard).enabled,     (shard_obj).enabled ); \
      REQUIRE_EQUAL_VARIANT( (reg_shard).opts,        (shard_obj).opts ); \
   }
BOOST_AUTO_TEST_SUITE(shard_tests)



BOOST_FIXTURE_TEST_CASE( register_shard_test, shard_base_tester ) try {

   // regshard( "alice"_n, shard1_name, true);
   registered_shard shard1 = registered_shard {
      .name             = shard1_name,
      .shard_type       = shard_type_enum(eosio::chain::shard_type::normal),
      .owner            = shard1_owner,
      .enabled          = true,
      .opts             = 0
   };

   BOOST_CHECK_EXCEPTION(regshard( shard1_owner, shard1, 1), unaccessible_api,
                           fc_exception_message_contains("shard.test does not have permission to call this API") );

   base_tester::push_action(config::system_account_name, "setpriv"_n, config::system_account_name,  fc::mutable_variant_object()("account", contract_name)("is_priv", 1));
   produce_blocks();
   regshard( shard1_owner, shard1, 1 );
   const auto* shard_change1 = control->dbm().main_db().find<shard_change_object, by_name>( shard1_name );
   BOOST_REQUIRE( shard_change1 != nullptr);

   REQUIRE_MATCHING_REGISTER_SHARD(shard1, *shard_change1);

   produce_blocks(2);
   const auto* shard_change2 = control->dbm().main_db().find<shard_change_object, by_name>( shard1_name );
   BOOST_REQUIRE( shard_change2 == nullptr);

   const auto* shard_obj1 = control->dbm().main_db().find<shard_object, by_name>( shard1_name );
   BOOST_REQUIRE( shard_obj1 != nullptr);

   REQUIRE_MATCHING_REGISTER_SHARD( shard1, *shard_obj1 );
   BOOST_REQUIRE( shard_obj1->creation_date == control->head_block_time() );

   BOOST_REQUIRE( control->dbm().find_shard_db(shard1_name) != nullptr);

   produce_blocks(1);

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()