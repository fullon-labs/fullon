#include <boost/test/unit_test.hpp>
#include <eosio/testing/tester.hpp>
#include <test_contracts.hpp>
#define TEST tester
using namespace eosio;
using namespace testing;
using namespace chain;

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
    BOOST_CHECK_EXCEPTION(create_account_on_subshard(t, "hello"_n, "test"_n), fc::exception, [](const fc::exception& e) {
      return e.to_detail_string().find("newaccount not allowed in sub shards") != std::string::npos;
    });
    t.produce_block();


}

BOOST_AUTO_TEST_SUITE_END()