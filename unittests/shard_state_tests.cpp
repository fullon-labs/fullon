#include <eosio/chain/config.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/testing/database_manager_fixture.hpp>

#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <eosio/chain/shard_object.hpp>
#include <test_contracts.hpp>
#include "dummy_data.hpp"
using namespace eosio;
using namespace testing;
using namespace chain;
using mvo = fc::mutable_variant_object;
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

static constexpr name contract_name   = "shard.test"_n;
static constexpr name shard1_name     = "shard1"_n;
static constexpr name shard1_owner    = "owner.shard1"_n;
static constexpr name shard2_name     = "shard2"_n;
static constexpr name shard2_owner    = "owner.shard2"_n;
class shard_base_tester : public tester {
   public:
   shard_base_tester(setup_policy policy = setup_policy::full, db_read_mode read_mode = db_read_mode::HEAD, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{}, std::optional<uint32_t> config_max_nonprivileged_inline_action_size = std::optional<uint32_t>{})
      :tester(policy, read_mode, genesis_max_inline_action_size, config_max_nonprivileged_inline_action_size)
   {
      produce_blocks();

      create_accounts( { contract_name, shard1_owner } );
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

class sharding_tester : public shard_base_tester {
     public:

      sharding_tester(setup_policy policy = setup_policy::full, db_read_mode read_mode = db_read_mode::HEAD, std::optional<uint32_t> genesis_max_inline_action_size = std::optional<uint32_t>{}, std::optional<uint32_t> config_max_nonprivileged_inline_action_size = std::optional<uint32_t>{})
         :shard_base_tester(policy, read_mode, genesis_max_inline_action_size, config_max_nonprivileged_inline_action_size)
      {
         registered_shard shard1 = registered_shard {
         .name             = shard1_name,
         .shard_type       = shard_type_enum(eosio::chain::shard_type::normal),
         .owner            = shard1_owner,
         .enabled          = true,
         .opts             = 0
         };

         base_tester::push_action(config::system_account_name, "setpriv"_n, config::system_account_name,  fc::mutable_variant_object()("account", contract_name)("is_priv", 1));
         produce_blocks();
         regshard( shard1_owner, shard1, 1 );
         produce_blocks(2);
      }

      signed_block_ptr produce_block( fc::microseconds skip_time = fc::milliseconds(config::block_interval_ms) )override {
         return _produce_block(skip_time, false);
      }

      signed_block_ptr produce_empty_block( fc::microseconds skip_time = fc::milliseconds(config::block_interval_ms) )override {
         abort_block();
         return _produce_block(skip_time, true);
      }

      signed_block_ptr finish_block()override {
         return _finish_block();
      }

      bool validate() { return true; }

      void set_transaction_headers( transaction& trx, uint32_t expiration = DEFAULT_EXPIRATION_DELTA, uint32_t delay_sec = 0 ) const {
         trx.expiration = control->head_block_time() + fc::seconds(expiration);
         trx.set_reference_block( control->head_block_id() );

         trx.max_net_usage_words = 0; // No limit
         trx.max_cpu_usage_ms = 0; // No limit
         trx.delay_sec = delay_sec;
      }
};

class sharding_validating_base_tester : public validating_tester{
   public:
      sharding_validating_base_tester(const flat_set<account_name>& trusted_producers = flat_set<account_name>(), deep_mind_handler* dmlog = nullptr)
         :validating_tester(trusted_producers, dmlog)
      {
         produce_blocks();
         create_accounts( { contract_name, shard2_owner, shard1_owner} );
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

      transaction_trace_ptr push_dummy(account_name from, const string& v, uint32_t billed_cpu_time_us, shard_name sname = config::main_shard_name) {
         // use reqauth for a normal action, this could be anything
         fc::variant pretty_trx = fc::mutable_variant_object()
            ("actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::system_account_name))
                  ("name", "reqauth")
                  ("authorization", fc::variants({
                     fc::mutable_variant_object()
                        ("actor", from)
                        ("permission", name(config::active_name))
                  }))
                  ("data", fc::mutable_variant_object()
                     ("from", from)
                  )
               })
         )
         // lets also push a context free action, the multi chain test will then also include a context free action
         ("context_free_actions", fc::variants({
               fc::mutable_variant_object()
                  ("account", name(config::null_account_name))
                  ("name", "nonce")
                  ("data", fc::raw::pack(v))
               })
            );

         signed_transaction trx;
         abi_serializer::from_variant(pretty_trx, trx, get_resolver(), abi_serializer::create_yield_function( abi_serializer_max_time ));
         set_transaction_headers(trx);
         trx.set_shard(sname);
         trx.sign( get_private_key( from, "active" ), control->get_chain_id() );
         return push_transaction( trx, fc::time_point::maximum(), billed_cpu_time_us );
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
class sharding_validating_tester : public sharding_validating_base_tester {
      public:
         virtual ~sharding_validating_tester(){

         }

         sharding_validating_tester(const flat_set<account_name>& trusted_producers = flat_set<account_name>(), deep_mind_handler* dmlog = nullptr)
         :sharding_validating_base_tester(trusted_producers, dmlog)
         {
            registered_shard shard1 = registered_shard {
            .name             = shard1_name,
            .shard_type       = shard_type_enum(eosio::chain::shard_type::normal),
            .owner            = shard1_owner,
            .enabled          = true,
            .opts             = 0
            };
            registered_shard shard2 = registered_shard {
            .name             = shard2_name,
            .shard_type       = shard_type_enum(eosio::chain::shard_type::normal),
            .owner            = shard2_owner,
            .enabled          = true,
            .opts             = 0
            };

            base_tester::push_action(config::system_account_name, "setpriv"_n, config::system_account_name,  fc::mutable_variant_object()("account", contract_name)("is_priv", 1));
            produce_blocks();
            regshard( shard1_owner, shard1, 1 );
            regshard( shard2_owner, shard2, 1 );
            produce_blocks(2);
         }

         void set_transaction_headers( transaction& trx, uint32_t expiration = DEFAULT_EXPIRATION_DELTA, uint32_t delay_sec = 0 ) const {
            trx.expiration = control->head_block_time() + fc::seconds(expiration);
            trx.set_reference_block( control->head_block_id() );

            trx.max_net_usage_words = 0; // No limit
            trx.max_cpu_usage_ms = 0; // No limit
            trx.delay_sec = delay_sec;
         }
   };

class currency_test : public sharding_validating_tester {
   public:

      auto push_action(const account_name& signer, const action_name &name, const variant_object &data, shard_name sname = config::main_shard_name ) {
         string action_type_name = abi_ser.get_action_type(name);

         action act;
         act.account = "flon.token"_n;
         act.name = name;
         act.authorization = vector<permission_level>{{signer, config::active_name}};
         act.data = abi_ser.variant_to_binary(action_type_name, data, abi_serializer::create_yield_function( abi_serializer_max_time ));

         signed_transaction trx;
         trx.set_shard(sname);
         trx.actions.emplace_back(std::move(act));

         set_transaction_headers(trx);
         trx.sign(get_private_key(signer, "active"), control->get_chain_id());
         return push_transaction(trx);
      }

      asset get_balance(const account_name& account) const {
         return get_currency_balance("flon.token"_n, symbol(SY(4,CUR)), account);
      }
      //TODO: simplify
      asset get_balance_on_shard(const account_name& account) const {
         const auto& gdb  = const_cast<database_manager&>(control->dbm()).find_shard_db("shard1"_n);
         const auto& db  = *gdb;
         account_name code = "flon.token"_n;
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
         auto trace = push_action("flon.token"_n, "issue"_n, mutable_variant_object()
                                  ("to",       to)
                                  ("quantity", quantity)
                                  ("memo",     memo)
                                  );
         produce_block();
         return trace;
      }

      currency_test()
         :sharding_validating_tester(),abi_ser(json::from_string(test_contracts::eosio_token_abi().data()).as<abi_def>(), abi_serializer::create_yield_function( abi_serializer_max_time ))
      {
         create_account( "flon.token"_n);
         produce_block();
         set_code( "flon.token"_n, test_contracts::eosio_token_wasm() );
         produce_block();
         auto result = push_action("flon.token"_n, "create"_n, mutable_variant_object()
                 ("issuer",       flon_token)
                 ("maximum_supply", "1000000000.0000 CUR")
                 ("can_freeze", 0)
                 ("can_recall", 0)
                 ("can_whitelist", 0),
                 "shard1"_n
         );
         result = push_action("flon.token"_n, "create"_n, mutable_variant_object()
                 ("issuer",       flon_token)
                 ("maximum_supply", "1000000000.0000 CUR")
                 ("can_freeze", 0)
                 ("can_recall", 0)
                 ("can_whitelist", 0),
                 "shard2"_n
         );
         wdump((result));
         produce_block();
         result = push_action("flon.token"_n, "issue"_n, mutable_variant_object()
                 ("to",       flon_token)
                 ("quantity", "1000000.0000 CUR")
                 ("memo", "gggggggggggg"),
                 "shard1"_n
         );
         result = push_action("flon.token"_n, "issue"_n, mutable_variant_object()
                 ("to",       flon_token)
                 ("quantity", "1000000.0000 CUR")
                 ("memo", "gggggggggggg"),
                 "shard2"_n
         );
         wdump((result));
         produce_block();
      }

      abi_serializer abi_ser;
      static constexpr name flon_token = "flon.token"_n;
};

transaction_trace_ptr create_account_on_subshard( sharding_tester& t, account_name a, account_name creator=config::system_account_name, bool multisig = false, bool include_code = true ){
      signed_transaction trx;
      trx.set_shard("shard1"_n);
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
    sharding_tester t;
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
      auto trace = push_action("flon.token"_n, "transfer"_n, mutable_variant_object()
         ("from", currency_test::flon_token)
         ("to",   "alice")
         ("quantity", "1.0000 CUR")
         ("memo", "fund Alice"),
         "shard1"_n
      );
      idump((trace));
      trace = push_action("flon.token"_n, "transfer"_n, mutable_variant_object()
         ("from", currency_test::flon_token)
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

BOOST_FIXTURE_TEST_CASE( shard_net_capacity_test, sharding_validating_tester ) try {
   BOOST_CHECK_NO_THROW(create_account("alice"_n));
   produce_block();

   //max_block_net 2*1024*1024 bytes, max_transaction_net max_block_net/2 bytes
   //dummy data 450000 bytes
   std::string dummy_string = dummy;
   uint32_t increment = 0;

   //3 consecutive concurrent transactions
   push_dummy( "alice"_n, dummy_string + std::to_string(0), increment );
   push_dummy( "alice"_n, dummy_string + std::to_string(1), increment , "shard1"_n);
   push_dummy( "alice"_n, dummy_string + std::to_string(2), increment, "shard2"_n );
   push_dummy( "alice"_n, dummy_string + std::to_string(3), increment, "shard1"_n );
   BOOST_CHECK_EXCEPTION(push_dummy( "alice"_n, dummy_string + std::to_string(4), increment, "shard2"_n ),
               block_net_usage_exceeded,
               fc_exception_message_contains("not enough space left in block")
   );
   produce_block();

   //3 consecutive concurrent transactions
   push_dummy( "alice"_n, dummy_string + std::to_string(1), increment , "shard1"_n);
   push_dummy( "alice"_n, dummy_string + std::to_string(0), increment );
   push_dummy( "alice"_n, dummy_string + std::to_string(2), increment, "shard2"_n );
   push_dummy( "alice"_n, dummy_string + std::to_string(3), increment, "shard1"_n );
   BOOST_CHECK_EXCEPTION(push_dummy( "alice"_n, dummy_string + std::to_string(4), increment, "shard2"_n ),
               block_net_usage_exceeded,
               fc_exception_message_contains("not enough space left in block")
   );
   produce_block();

   //4 consecutive concurrent transactions
   push_dummy( "alice"_n, dummy_string + std::to_string(1), increment, "shard1"_n );
   push_dummy( "alice"_n, dummy_string + std::to_string(0), increment );
   push_dummy( "alice"_n, dummy_string + std::to_string(2), increment, "shard2"_n );
   push_dummy( "alice"_n, dummy_string + std::to_string(3), increment, "shard1"_n );
   BOOST_CHECK_EXCEPTION(push_dummy( "alice"_n, dummy_string + std::to_string(4), increment, "shard2"_n ),
               block_net_usage_exceeded,
               fc_exception_message_contains("not enough space left in block")
   );
   BOOST_CHECK_EXCEPTION(push_dummy( "alice"_n, dummy_string + std::to_string(5), increment, "shard1"_n ),
               block_net_usage_exceeded,
               fc_exception_message_contains("not enough space left in block")
   );
} FC_LOG_AND_RETHROW ();

BOOST_AUTO_TEST_SUITE_END()