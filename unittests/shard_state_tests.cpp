#include <eosio/chain/config.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/testing/database_manager_fixture.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#define TEST tester
using namespace eosio;
using namespace testing;
using namespace chain;

class resource_limits_fixture: private database_manager_fixture<1024*1024>, public resource_limits_manager
{
   public:
      resource_limits_fixture()
      :database_manager_fixture()
      ,resource_limits_manager(*database_manager_fixture::_dbm, [](bool) { return nullptr; })
      {
         add_indices((*database_manager_fixture::_dbm).main_db());
         initialize_database();
      }

      ~resource_limits_fixture() {}

      chainbase::database::session start_session() {
         return database_manager_fixture::_dbm->main_db().start_undo_session(true);
      }

      chainbase::database& get_main() { return (*database_manager_fixture::_dbm).main_db();}
      chainbase::database& get_shared() { return (*database_manager_fixture::_dbm).shared_db();}
};

class currency_test : public validating_tester {
   public:

      auto push_action(const account_name& signer, const action_name &name, const variant_object &data, shard_name sname = config::main_shard_name ) {
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = "gax.token"_n;
         act.name = name;
         act.authorization = vector<permission_level>{{signer, config::active_name}};
         act.data = abi_ser.variant_to_binary(action_type_name, data, abi_serializer::create_yield_function( abi_serializer_max_time ));

         signed_transaction trx;
         trx.set_shard_name(sname);
         trx.actions.emplace_back(std::move(act));

         set_transaction_headers(trx);
         trx.sign(get_private_key(signer, "active"), control->get_chain_id());
         return push_transaction(trx);
      }

      asset get_balance(const account_name& account) const {
         return get_currency_balance("gax.token"_n, symbol(SY(4,CUR)), account);
      }
      //TODO: simplify
      asset get_balance_on_shard(const account_name& account) const {
         const auto& gdb  = const_cast<database_manager&>(control->dbm()).find_shard_db("shard1"_n);
         const auto& db  = *gdb;
         account_name code = "gax.token"_n;
         symbol asset_symbol(SY(4,CUR));
         const auto* tbl = db.template find<table_id_object, by_code_scope_table >(boost::make_tuple(code, account, "accounts"_n));
         share_type result = 0;

         //the balance is implied to be 0 if either the table or row does not exist
         if (tbl) {
            const auto *obj = db.template find<key_value_object, by_scope_primary>(boost::make_tuple(tbl->id, asset_symbol.to_symbol_code().value));
            if (obj) {
               //balance is the first field in the serialization
               fc::datastream<const char *> ds(obj->value.data(), obj->value.size());
               fc::raw::unpack(ds, result);
            }
         }
         return asset(result, asset_symbol);
      }

      auto transfer(const account_name& from, const account_name& to, const std::string& quantity, const std::string& memo = "") {
         auto trace = push_action(from, "transfer"_n, mutable_variant_object()
                                  ("from",     from)
                                  ("to",       to)
                                  ("quantity", quantity)
                                  ("memo",     memo)
                                  );
         produce_block();
         return trace;
      }

      auto issue(const account_name& to, const std::string& quantity, const std::string& memo = "") {
         auto trace = push_action("gax.token"_n, "issue"_n, mutable_variant_object()
                                  ("to",       to)
                                  ("quantity", quantity)
                                  ("memo",     memo)
                                  );
         produce_block();
         return trace;
      }

      currency_test()
         :validating_tester(),abi_ser(json::from_string(test_contracts::eosio_token_abi().data()).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time ))
      {
         create_account( "gax.token"_n);
         produce_block();
         set_code( "gax.token"_n, test_contracts::eosio_token_wasm() );
         produce_block();
         auto result = push_action("gax.token"_n, "create"_n, mutable_variant_object()
                 ("issuer",       gax_token)
                 ("maximum_supply", "1000000000.0000 CUR")
                 ("can_freeze", 0)
                 ("can_recall", 0)
                 ("can_whitelist", 0),
                 "shard1"_n
         );
         result = push_action("gax.token"_n, "create"_n, mutable_variant_object()
                 ("issuer",       gax_token)
                 ("maximum_supply", "1000000000.0000 CUR")
                 ("can_freeze", 0)
                 ("can_recall", 0)
                 ("can_whitelist", 0),
                 "shard2"_n
         );
         wdump((result));
         produce_block();
         result = push_action("gax.token"_n, "issue"_n, mutable_variant_object()
                 ("to",       gax_token)
                 ("quantity", "1000000.0000 CUR")
                 ("memo", "gggggggggggg"),
                 "shard1"_n
         );
         result = push_action("gax.token"_n, "issue"_n, mutable_variant_object()
                 ("to",       gax_token)
                 ("quantity", "1000000.0000 CUR")
                 ("memo", "gggggggggggg"),
                 "shard2"_n
         );
         wdump((result));
         produce_block();
      }

      abi_serializer abi_ser;
      static constexpr name gax_token = "gax.token"_n;
};

transaction_trace_ptr create_account_on_subshard( tester& t, account_name a, account_name creator=config::system_account_name, bool multisig = false, bool include_code = true ){
      signed_transaction trx;
      trx.set_shard_name("shard1"_n);
      t.set_transaction_headers(trx);

      authority owner_auth;
      if( multisig ) {
         // multisig between account's owner key and creators active permission
         owner_auth = authority(2, {key_weight{t.get_public_key( a, "owner" ), 1}}, {permission_level_weight{{creator, config::active_name}, 1}});
      } else {
         owner_auth =  authority( t.get_public_key( a, "owner" ) );
      }

      authority active_auth( t.get_public_key( a, "active" ) );

      auto sort_permissions = []( authority& auth ) {
         std::sort( auth.accounts.begin(), auth.accounts.end(),
                    []( const permission_level_weight& lhs, const permission_level_weight& rhs ) {
                        return lhs.permission < rhs.permission;
                    }
                  );
      };

      if( include_code ) {
         FC_ASSERT( owner_auth.threshold <= std::numeric_limits<weight_type>::max(), "threshold is too high" );
         FC_ASSERT( active_auth.threshold <= std::numeric_limits<weight_type>::max(), "threshold is too high" );
         owner_auth.accounts.push_back( permission_level_weight{ {a, config::eosio_code_name},
                                                                 static_cast<weight_type>(owner_auth.threshold) } );
         sort_permissions(owner_auth);
         active_auth.accounts.push_back( permission_level_weight{ {a, config::eosio_code_name},
                                                                  static_cast<weight_type>(active_auth.threshold) } );
         sort_permissions(active_auth);
      }

      trx.actions.emplace_back( vector<permission_level>{{creator,config::active_name}},
                                newaccount{
                                   .creator  = creator,
                                   .name     = a,
                                   .owner    = owner_auth,
                                   .active   = active_auth,
                                });

      t.set_transaction_headers(trx);
      trx.sign( t.get_private_key( creator, "active" ), t.control->get_chain_id()  );
      return t.push_transaction( trx );
   }

BOOST_AUTO_TEST_SUITE(shard_state_tests)

BOOST_AUTO_TEST_CASE(query_account_object_from_share_db_test){
    tester t;
    //system account create account test on main.
    BOOST_CHECK_NO_THROW(t.create_account("test"_n));
    t.produce_block();
    //account test create account hello on shard1.
    BOOST_CHECK_EXCEPTION(create_account_on_subshard(t, "hello"_n, "test"_n),
               only_main_shard_allowed_exception,
               fc_exception_message_contains("newaccount only allowed in main shard")
    );
    t.produce_block();

}

BOOST_AUTO_TEST_CASE(shard_shared_state_test ) try {
   tester t;
   BOOST_CHECK_NO_THROW(t.create_account("alice"_n));
   BOOST_CHECK_NO_THROW(t.create_account("bob"_n));
   t.produce_block();
   auto& dbm = const_cast< eosio::chain::database_manager&>(t.control->dbm());
   resource_limits_manager rlm(dbm, [](bool) { return nullptr; });
   chainbase::database& main_db = dbm.main_db();
   chainbase::database& shared_db = dbm.shared_db();
   // rlm.initialize_account("alice"_n, false);
   rlm.set_account_limits("alice"_n, 1024*1024, 100, 99, main_db, false);
   // rlm.initialize_account("bob"_n, false);
   rlm.set_account_limits("bob"_n, 1024*1024, 100, 99, main_db, false);
   t.produce_block();
   rlm.process_account_limit_updates();
   int64_t ram_bytes; int64_t net_weight; int64_t cpu_weight;
   rlm.get_account_limits( "alice"_n, ram_bytes, net_weight, cpu_weight, shared_db);
   BOOST_REQUIRE_EQUAL( ram_bytes, 1024*1024 );
   BOOST_REQUIRE_EQUAL( net_weight, 100 );
   BOOST_REQUIRE_EQUAL( cpu_weight, 99 );
} FC_LOG_AND_RETHROW ();


BOOST_FIXTURE_TEST_CASE( shard_tx_test, currency_test ) try {
   BOOST_CHECK_NO_THROW(create_account("alice"_n));
   BOOST_CHECK_NO_THROW(create_account("bob"_n));
   produce_block();
   int loop = 10000;
   for( int i=1 ; i< loop ; i++ ){
      auto trace = push_action("gax.token"_n, "transfer"_n, mutable_variant_object()
         ("from", currency_test::gax_token)
         ("to",   "alice")
         ("quantity", "1.0000 CUR")
         ("memo", "fund Alice"),
         "shard1"_n
      );
      idump((trace));
      trace = push_action("gax.token"_n, "transfer"_n, mutable_variant_object()
         ("from", currency_test::gax_token)
         ("to",   "alice")
         ("quantity", "1.0000 CUR")
         ("memo", "fund Alice"),
         "shard2"_n
      );
      idump((trace));
      produce_block();
      BOOST_TEST_MESSAGE("has produced: "<<i);
      //BOOST_TEST_MESSAGE(control->head_block_num());
      BOOST_REQUIRE_EQUAL(true, chain_has_transaction(trace->id));
   }
   std::string value = std::to_string(1*(loop-1))+".0000 CUR";
   // BOOST_REQUIRE_EQUAL(get_balance_on_shard("alice"_n), asset::from_string( "500.0000 CUR" ) );
   BOOST_REQUIRE_EQUAL(get_balance_on_shard("alice"_n), asset::from_string( value ) );
} FC_LOG_AND_RETHROW ();

BOOST_AUTO_TEST_SUITE_END()