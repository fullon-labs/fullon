#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/exception/exception.hpp>
#include <fc/variant_object.hpp>

#include <contracts.hpp>

#include "eosio_system_tester.hpp"
#include <eosio/chain/shard_object.hpp>
#include <eosio/chain/xshard_object.hpp>

struct xtoken {
   asset             quantity;
   std::string       memo;
};
FC_REFLECT( xtoken, (quantity)(memo) )

class xshard_tester : public eosio_system::eosio_system_tester {
public:
   static constexpr name shard1_name     = "sub.shard1"_n;
   static constexpr name shard1_owner    = "owner.shard1"_n;

   xshard_tester()
   {
      produce_blocks();
      create_account_with_resources( shard1_owner, config::system_account_name, core_from_string("10.0000"), false );
      regshard(config::system_account_name, shard1_name, shard1_owner, true);
      produce_blocks(2);
      const auto* shard_obj1 = control->dbm().main_db().find<shard_object, by_name>( shard1_name );
      BOOST_REQUIRE( shard_obj1 != nullptr);
   }

   transaction_trace_ptr    push_action( const account_name& code,
                                          const action_name& acttype,
                                          const account_name& actor,
                                          const variant_object& data,
                                          uint32_t expiration = DEFAULT_EXPIRATION_DELTA,
                                          uint32_t delay_sec = 0 )
   {
      return base_tester::push_action( code, acttype, actor, data, expiration, delay_sec );
   }

   transaction_trace_ptr regshard( const account_name& signer, const eosio::chain::name& name, const eosio::chain::name& owner, bool enabled )
   {
   // transaction_trace_ptr base_tester::push_action( const account_name& code,
   //                                                 const action_name& acttype,
   //                                                 const account_name& actor,
   //                                                 const variant_object& data,
   //                                                 uint32_t expiration,
   //                                                 uint32_t delay_sec
   //                                               )

      return push_action( config::system_account_name, "regshard"_n, signer, mvo()
            ( "name",             name)
            ( "owner",            owner)
            ( "enabled",          enabled)
      );
   }

   transaction_trace_ptr xshout( const name&                owner,
                                 const name&                to_shard,
                                 const name&                contract,
                                 const name&                action_name,
                                 const std::vector<char>&   action_data )
   {
      return push_action( config::system_account_name, "xshout"_n, owner, mvo()
            ( "owner",            owner )
            ( "to_shard",         to_shard )
            ( "contract",         contract )
            ( "action_name",      action_name )
            ( "action_data",      action_data )
      );
   }

   // void xshin( const name& owner, const checksum256& xsh_id );
   transaction_trace_ptr xshin( const name&                owner,
                                 const xshard_id_type&     xsh_id )
   {
      return push_action( config::system_account_name, "xshin"_n, owner, mvo()
            ( "owner",           owner )
            ( "xsh_id",          xsh_id )
      );
   }

   vector<char> get_row_by_account( const database& db, name code, name scope, name table, const account_name& act ) const {
      vector<char> data;
      const auto* t_id = db.find<table_id_object, by_code_scope_table>( boost::make_tuple( code, scope, table ) );
      if ( !t_id ) {
         return data;
      }
      //FC_ASSERT( t_id != 0, "object not found" );

      const auto& idx = db.get_index<key_value_index, by_scope_primary>();

      auto itr = idx.lower_bound( boost::make_tuple( t_id->id, act.to_uint64_t() ) );
      if ( itr == idx.end() || itr->t_id != t_id->id || act.to_uint64_t() != itr->primary_key ) {
         return data;
      }

      data.resize( itr->value.size() );
      memcpy( data.data(), itr->value.data(), data.size() );
      return data;
   }

   asset get_balance( const database& db, const account_name& act ) {
      vector<char> data = get_row_by_account( db, "gax.token"_n, act, "accounts"_n, name(symbol(CORE_SYMBOL).to_symbol_code().value) );
      return data.empty() ? asset(0, symbol(CORE_SYMBOL)) : token_abi_ser.binary_to_variant("account", data, abi_serializer::create_yield_function( abi_serializer_max_time ))["balance"].as<asset>();
   }
};


/*
 * register test suite `xshard_tests`
 */
BOOST_AUTO_TEST_SUITE(xshard_tests)

/*************************************************************************************
 * xshard_tests test case
 *************************************************************************************/

BOOST_FIXTURE_TEST_CASE( xshard_transfer_test, xshard_tester ) try {
   const auto& dbm = control->dbm();
   const auto& main_db = dbm.main_db();
   const auto* shard1_db = dbm.find_shard_db(shard1_name);
   if (!shard1_db) BOOST_ERROR("shard1 db not found");
   transfer(config::system_account_name, "alice1111111"_n, core_from_string("100.0000"));
   auto xshout_data = fc::raw::pack( xtoken{core_from_string("1.0000"), ""} );
   auto xshout_trx = xshout( "alice1111111"_n, shard1_name, "gax.token"_n, "xtoken"_n, xshout_data );
   BOOST_REQUIRE_EQUAL(get_balance(main_db, "alice1111111"_n), core_from_string("99.0000"));

   const auto& xsh_indx = control->dbm().shared_db().get_index<xshard_index, by_id>();
   auto xsh_id = xshard_object::make_xsh_id(xshout_trx->id, 0);

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 1);
   auto xsh_itr = xsh_indx.begin();
   BOOST_REQUIRE(xsh_itr != xsh_indx.end());
   BOOST_REQUIRE_EQUAL(xsh_itr->xsh_id, xsh_id);
   BOOST_REQUIRE_EQUAL(xsh_itr->owner, "alice1111111"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr->from_shard, config::main_shard_name);
   BOOST_REQUIRE_EQUAL(xsh_itr->to_shard, shard1_name);
   BOOST_REQUIRE_EQUAL(xsh_itr->contract, "gax.token"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr->action_type, "xtoken"_n);
   BOOST_REQUIRE(bytes(xsh_itr->action_data.begin(), xsh_itr->action_data.end())  == xshout_data);

   static const asset& balance0 = core_from_string("0.0000");
   BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "alice1111111"_n), balance0);
   {
      shard_name_scope scope(*this, shard1_name);
      xshin("alice1111111"_n, xsh_id);
   }
   BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "alice1111111"_n), core_from_string("1.0000"));

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);

   // const auto* shard_obj1 = control->dbm().main_db().find<shard_object, by_name>( shard1_name );

} FC_LOG_AND_RETHROW()
BOOST_AUTO_TEST_SUITE_END()
