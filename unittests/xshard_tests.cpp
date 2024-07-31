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
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/generated_transaction_object.hpp>

struct xtransfer {
   asset             quantity;
   std::string       memo;
};
FC_REFLECT( xtransfer, (quantity)(memo) )

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
                                 const name&                action_type,
                                 const std::vector<char>&   action_data )
   {
      return push_action( config::system_account_name, "xshout"_n, owner, mvo()
            ( "owner",            owner )
            ( "to_shard",         to_shard )
            ( "contract",         contract )
            ( "action_type",      action_type )
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

   template<typename T = contract_tables>
   vector<char> get_row_by_account( const database& db, name code, name scope, name table, const account_name& act ) const {
      using table_id_object = typename T::table_id_object;
      using key_value_object = typename T::key_value_object;
      using key_value_index = typename chainbase::get_index_type<key_value_object>::type;
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
      vector<char> data = get_row_by_account( db, "flon.token"_n, act, "accounts"_n, name(symbol(CORE_SYMBOL).to_symbol_code().value) );
      return data.empty() ? asset(0, symbol(CORE_SYMBOL)) : token_abi_ser.binary_to_variant("account", data, abi_serializer::create_yield_function( abi_serializer_max_time ))["balance"].as<asset>();
   }
   fc::variant get_stats( const database& db, const eosio::chain::symbol_code& symb_code ) {
      auto symb_name =  name(symb_code.value);
      vector<char> data = get_row_by_account<contract_shared_tables>(
            db, "flon.token"_n, symb_name, "stat"_n, symb_name );
      return data.empty() ? fc::variant() : token_abi_ser.binary_to_variant( "currency_stats", data, abi_serializer::create_yield_function( abi_serializer_max_time ) );
   }

   fc::variant get_stats( const database& db, const eosio::chain::symbol& symb ) {
      return get_stats(db, symb.to_symbol_code());
   }

   fc::variant get_stats( const database& db, const string& symb ) {
      return get_stats(db, eosio::chain::symbol::from_string(symb));
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
   const auto& shared_db = dbm.shared_db();
   const auto* shard1_db = dbm.find_shard_db(shard1_name);

   wdump((get_stats(main_db, core_symbol))(get_stats(shared_db, core_symbol)));

   REQUIRE_EQUAL_OBJECTS( get_stats(main_db, core_symbol), get_stats(shared_db, core_symbol));

   if (!shard1_db) BOOST_ERROR("shard1 db not found");
   transfer(config::system_account_name, "alice1111111"_n, core_from_string("1000.0000"));
   BOOST_REQUIRE_EQUAL(get_balance(main_db, "alice1111111"_n), core_from_string("1000.0000"));

   auto xshout_data = fc::raw::pack( xtransfer{core_from_string("100.0000"), ""} );
   auto xshout_trx = xshout( "alice1111111"_n, shard1_name, "flon.token"_n, "xtransfer"_n, xshout_data );

   const auto& xsh_indx = shared_db.get_index<xshard_index, by_id>();
   const auto& gen_trx_indx = shared_db.get_index<generated_transaction_multi_index, by_trx_id>();
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
   BOOST_REQUIRE_EQUAL(xsh_itr->contract, "flon.token"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr->action_type, "xtransfer"_n);
   BOOST_REQUIRE(bytes(xsh_itr->action_data.begin(), xsh_itr->action_data.end())  == xshout_data);
   auto scheduled_xshin_trx = xsh_itr->scheduled_xshin_trx;
   BOOST_REQUIRE(scheduled_xshin_trx != transaction_id_type());

   const auto& gpo = control->db().get<global_property_object>();

   auto schedule_trx_obj = gen_trx_indx.find(scheduled_xshin_trx);
   BOOST_REQUIRE(schedule_trx_obj != gen_trx_indx.end());
   BOOST_REQUIRE_EQUAL(schedule_trx_obj->sender, xsh_itr->owner);
   wdump((schedule_trx_obj->sender_id)((uint128_t)xsh_itr->id._id));
   wdump((*schedule_trx_obj));
   BOOST_REQUIRE(uint64_t(schedule_trx_obj->sender_id >> 64) == ("xshard"_n).to_uint64_t());
   BOOST_REQUIRE(uint64_t(schedule_trx_obj->sender_id) == xsh_itr->id._id);
   BOOST_REQUIRE_EQUAL(schedule_trx_obj->payer, name());
   BOOST_REQUIRE(schedule_trx_obj->published == control->head_block_time());
   BOOST_REQUIRE(schedule_trx_obj->delay_until == schedule_trx_obj->published + fc::milliseconds(config::block_interval_ms));
   BOOST_REQUIRE(schedule_trx_obj->expiration == schedule_trx_obj->delay_until + fc::seconds(gpo.configuration.deferred_trx_expiration_window));
   BOOST_REQUIRE_EQUAL(schedule_trx_obj->is_xshard, true);

   static const asset& balance0 = core_from_string("0.0000");
   BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "alice1111111"_n), balance0);
   {
      shard_name_scope scope(*this, shard1_name);
      xshin("alice1111111"_n, xsh_id);
   }
   BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "alice1111111"_n), core_from_string("100.0000"));
   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);
   BOOST_REQUIRE_EQUAL(gen_trx_indx.size(), 0);
   BOOST_REQUIRE(gen_trx_indx.find(scheduled_xshin_trx) == gen_trx_indx.end());

   auto xshout_data2 = fc::raw::pack( xtransfer{core_from_string("100.0000"), ""} );

   transfer(config::system_account_name, "bob111111111"_n, core_from_string("1000.0000"));

   auto xshout_trx2 = xshout( "bob111111111"_n, shard1_name, "flon.token"_n, "xtransfer"_n, xshout_data2 );
   BOOST_REQUIRE_EQUAL(get_balance(main_db, "bob111111111"_n), core_from_string("900.0000"));

   auto xsh_id2 = xshard_object::make_xsh_id(xshout_trx2->id, 0);

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 1);
   auto xsh_itr2 = xsh_indx.begin();
   BOOST_REQUIRE(xsh_itr2 != xsh_indx.end());

   BOOST_REQUIRE_EQUAL(xsh_itr2->xsh_id, xsh_id2);
   BOOST_REQUIRE_EQUAL(xsh_itr2->owner, "bob111111111"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr2->from_shard, config::main_shard_name);
   BOOST_REQUIRE_EQUAL(xsh_itr2->to_shard, shard1_name);
   BOOST_REQUIRE_EQUAL(xsh_itr2->contract, "flon.token"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr2->action_type, "xtransfer"_n);
   BOOST_REQUIRE(bytes(xsh_itr2->action_data.begin(), xsh_itr2->action_data.end())  == xshout_data2);
   auto scheduled_xshin_trx2 = xsh_itr2->scheduled_xshin_trx;
   BOOST_REQUIRE(scheduled_xshin_trx2 != transaction_id_type());
   BOOST_REQUIRE(scheduled_xshin_trx2 != scheduled_xshin_trx);

   auto schedule_trx_obj2 = gen_trx_indx.find(scheduled_xshin_trx2);
   BOOST_REQUIRE(schedule_trx_obj2 != gen_trx_indx.end());
   BOOST_REQUIRE_EQUAL(schedule_trx_obj2->sender, xsh_itr2->owner);
   BOOST_REQUIRE(uint64_t(schedule_trx_obj2->sender_id >> 64) == ("xshard"_n).to_uint64_t());
   BOOST_REQUIRE(uint64_t(schedule_trx_obj2->sender_id) == xsh_itr2->id._id);
   BOOST_REQUIRE_EQUAL(schedule_trx_obj2->payer, name());
   BOOST_REQUIRE(schedule_trx_obj2->published == control->head_block_time());
   BOOST_REQUIRE(schedule_trx_obj2->delay_until == schedule_trx_obj2->published + fc::milliseconds(config::block_interval_ms));
   BOOST_REQUIRE(schedule_trx_obj2->expiration == schedule_trx_obj2->delay_until + fc::seconds(gpo.configuration.deferred_trx_expiration_window));
   BOOST_REQUIRE_EQUAL(schedule_trx_obj2->is_xshard, true);
   BOOST_REQUIRE_EQUAL(schedule_trx_obj2->shard_name, shard1_name);

   {
      fc::datastream<const char*> ds( schedule_trx_obj2->packed_trx.data(), schedule_trx_obj2->packed_trx.size() );
      signed_transaction schedule_trx2;
      fc::raw::unpack(ds, static_cast<transaction&>(schedule_trx2) );
      BOOST_REQUIRE(schedule_trx2.has_shard_extension());
      BOOST_REQUIRE_EQUAL(schedule_trx2.get_shard_name(), shard1_name);
   }

   {
      // shard_name_scope scope(*this, shard1_name);
      auto billed_cpu_time_us = gpo.configuration.min_transaction_cpu_usage;
      auto scheduled_trx_trace = push_scheduled_transaction(shard1_name, scheduled_xshin_trx2, fc::time_point::maximum(), fc::microseconds::maximum(), billed_cpu_time_us, true);
      BOOST_REQUIRE( !scheduled_trx_trace->except_ptr && !scheduled_trx_trace->except );
      BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "bob111111111"_n), core_from_string("100.0000"));
   }

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);
   BOOST_REQUIRE(gen_trx_indx.find(scheduled_xshin_trx2) == gen_trx_indx.end());

   // xtransfer from shard1 to main shard
   xshard_id_type xsh_id3;
   auto xshout_data3 = fc::raw::pack( xtransfer{core_from_string("10.0000"), ""} );
   {
      shard_name_scope scope(*this, shard1_name);
      auto xshout_trx3 = xshout( "bob111111111"_n, config::main_shard_name, "flon.token"_n, "xtransfer"_n, xshout_data3 );
      xsh_id3 = xshard_object::make_xsh_id(xshout_trx3->id, 0);
   }
   BOOST_REQUIRE_EQUAL(get_balance(*shard1_db, "bob111111111"_n), core_from_string("90.0000"));


   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 1);
   auto xsh_itr3 = xsh_indx.begin();
   BOOST_REQUIRE(xsh_itr3 != xsh_indx.end());

   BOOST_REQUIRE_EQUAL(xsh_itr3->xsh_id, xsh_id3);
   BOOST_REQUIRE_EQUAL(xsh_itr3->owner, "bob111111111"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr3->from_shard, shard1_name);
   BOOST_REQUIRE_EQUAL(xsh_itr3->to_shard, config::main_shard_name);
   BOOST_REQUIRE_EQUAL(xsh_itr3->contract, "flon.token"_n);
   BOOST_REQUIRE_EQUAL(xsh_itr3->action_type, "xtransfer"_n);
   BOOST_REQUIRE(bytes(xsh_itr3->action_data.begin(), xsh_itr3->action_data.end())  == xshout_data3);
   auto scheduled_xshin_trx3 = xsh_itr3->scheduled_xshin_trx;
   BOOST_REQUIRE(scheduled_xshin_trx3 != transaction_id_type());
   BOOST_REQUIRE(scheduled_xshin_trx3 != scheduled_xshin_trx2);

   auto schedule_trx_obj3 = gen_trx_indx.find(scheduled_xshin_trx3);
   BOOST_REQUIRE(schedule_trx_obj3 != gen_trx_indx.end());
   BOOST_REQUIRE_EQUAL(schedule_trx_obj3->sender, xsh_itr3->owner);
   BOOST_REQUIRE(uint64_t(schedule_trx_obj3->sender_id >> 64) == ("xshard"_n).to_uint64_t());
   BOOST_REQUIRE(uint64_t(schedule_trx_obj3->sender_id) == xsh_itr3->id._id);
   BOOST_REQUIRE_EQUAL(schedule_trx_obj3->payer, name());
   BOOST_REQUIRE(schedule_trx_obj3->published == control->head_block_time());
   BOOST_REQUIRE(schedule_trx_obj3->delay_until == schedule_trx_obj3->published + fc::milliseconds(config::block_interval_ms));
   BOOST_REQUIRE(schedule_trx_obj3->expiration == schedule_trx_obj3->delay_until + fc::seconds(gpo.configuration.deferred_trx_expiration_window));
   BOOST_REQUIRE_EQUAL(schedule_trx_obj3->is_xshard, true);
   BOOST_REQUIRE_EQUAL(schedule_trx_obj3->shard_name, config::main_shard_name);

   {
      fc::datastream<const char*> ds( schedule_trx_obj3->packed_trx.data(), schedule_trx_obj3->packed_trx.size() );
      signed_transaction schedule_trx3;
      fc::raw::unpack(ds, static_cast<transaction&>(schedule_trx3) );
      BOOST_REQUIRE(schedule_trx3.has_shard_extension());
      BOOST_REQUIRE_EQUAL(schedule_trx3.get_shard_name(), config::main_shard_name);
   }

   {
      // shard_name_scope scope(*this, shard1_name);
      auto billed_cpu_time_us = gpo.configuration.min_transaction_cpu_usage;
      auto scheduled_trx_trace = push_scheduled_transaction(config::main_shard_name, scheduled_xshin_trx3, fc::time_point::maximum(), fc::microseconds::maximum(), billed_cpu_time_us, true);
      BOOST_REQUIRE( !scheduled_trx_trace->except_ptr && !scheduled_trx_trace->except );
      BOOST_REQUIRE_EQUAL(get_balance(main_db, "bob111111111"_n), core_from_string("910.0000"));
   }

   produce_blocks();

   BOOST_REQUIRE_EQUAL(xsh_indx.size(), 0);
   BOOST_REQUIRE(gen_trx_indx.find(scheduled_xshin_trx3) == gen_trx_indx.end());

} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
