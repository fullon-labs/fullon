#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/database_header_object.hpp>
#include <eosio/chain/database_manager.hpp>
#include <eosio/testing/tester.hpp>

#include <fc/crypto/digest.hpp>

#include <boost/test/unit_test.hpp>

#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif

using namespace eosio::chain;
using namespace chainbase;
using namespace eosio::testing;
namespace bfs = boost::filesystem;


bool include_delta(const account_metadata_object& old, const account_metadata_object& curr) {
   return
      old.name != curr.name ||
      old.recv_sequence != curr.recv_sequence ||
      old.is_privileged() != curr.is_privileged() ||
      old.last_code_update != curr.last_code_update ||
      old.vm_type != curr.vm_type ||
      old.vm_version != curr.vm_version ||
      old.code_hash != curr.code_hash;
}

bool is_equal(const account_object& a, const account_object& b) {
   wdump(   (a.id == b.id)
            (a.name == b.name)
            (a.creation_date == b.creation_date)
            (a.abi == b.abi) );
   account_object c = a;
   wdump((a.abi.size())(b.abi.size())(c.abi.size())(a.abi == c.abi));
   // wdump((a.abi)(b.abi));
   return   a.id == b.id &&
            a.name == b.name &&
            a.creation_date == b.creation_date &&
            a.abi == b.abi;
}

template<typename ObjectType>
bool is_equal(const std::vector<ObjectType>& a, std::vector<ObjectType>& b) {
   if (a.size() != b.size()) {
      wdump((a.size())(b.size()));
      return false;
   }

   for (size_t i = 0; i < a.size(); i++) {
      if (!is_equal(a[i], b[i])) {
         wdump((i)(a[i].id)(a[i])(b[i].id)(b[i]));
         return false;
      }
   }
   return true;
}

template<typename IndexType>
struct index_helper {
   using value_type = typename IndexType::value_type;
   static std::vector<value_type> get_rows(const chainbase::database& db) {
      std::vector<value_type> ret;
      index_utils<IndexType>::walk(db, [&ret]( const auto& table_row ){
         ret.push_back(table_row);
      });
      return ret;
   }
};

BOOST_AUTO_TEST_SUITE(database_tests)

   // Simple tests of undo infrastructure
   BOOST_AUTO_TEST_CASE(undo_test) {
      try {
         TESTER test;

         // Bypass read-only restriction on state DB access for this unit test which really needs to mutate the DB to properly conduct its test.
         eosio::chain::database& db = const_cast<eosio::chain::database&>( test.control->db() );

         auto ses = db.start_undo_session(true);

         // Create an account
         db.create<account_object>([](account_object &a) {
            a.name = name("billy");
         });

         // Make sure we can retrieve that account by name
         auto ptr = db.find<account_object, by_name>(name("billy"));
         BOOST_TEST(ptr != nullptr);

         // Undo creation of the account
         ses.undo();

         // Make sure we can no longer find the account
         ptr = db.find<account_object, by_name>(name("billy"));
         BOOST_TEST(ptr == nullptr);
      } FC_LOG_AND_RETHROW()
   }

   // Test the block fetching methods on database, fetch_bock_by_id, and fetch_block_by_number
   BOOST_AUTO_TEST_CASE(get_blocks) {
      try {
         TESTER test;
         vector<block_id_type> block_ids;

         const uint32_t num_of_blocks_to_prod = 200;
         // Produce 200 blocks and check their IDs should match the above
         test.produce_blocks(num_of_blocks_to_prod);
         for (uint32_t i = 0; i < num_of_blocks_to_prod; ++i) {
            block_ids.emplace_back(test.control->fetch_block_by_number(i + 1)->calculate_id());
            BOOST_TEST(block_header::num_from_id(block_ids.back()) == i + 1);
            BOOST_TEST(test.control->fetch_block_by_number(i + 1)->calculate_id() == block_ids.back());
         }

         // Check the last irreversible block number is set correctly, with one producer, irreversibility should only just 1 block before
         const auto expected_last_irreversible_block_number = test.control->head_block_num() - 1;
         BOOST_TEST(test.control->head_block_state()->dpos_irreversible_blocknum == expected_last_irreversible_block_number);
         // Ensure that future block doesn't exist
         const auto nonexisting_future_block_num = test.control->head_block_num() + 1;
         BOOST_TEST(test.control->fetch_block_by_number(nonexisting_future_block_num) == nullptr);

         const uint32_t next_num_of_blocks_to_prod = 100;
         test.produce_blocks(next_num_of_blocks_to_prod);

         const auto next_expected_last_irreversible_block_number = test.control->head_block_num() - 1;
         // Check the last irreversible block number is updated correctly
         BOOST_TEST(test.control->head_block_state()->dpos_irreversible_blocknum == next_expected_last_irreversible_block_number);
         // Previous nonexisting future block should exist by now
         BOOST_CHECK_NO_THROW(test.control->fetch_block_by_number(nonexisting_future_block_num));
         // Check the latest head block match
         BOOST_TEST(test.control->fetch_block_by_number(test.control->head_block_num())->calculate_id() ==
                    test.control->head_block_id());
      } FC_LOG_AND_RETHROW()
   }

   // Simple tests of database read/write
   BOOST_AUTO_TEST_CASE(db_read_write) {
      try {
         fc::temp_directory tempdir;
         auto state_dir = tempdir.path() / "state";
         wdump((state_dir));
         uint64_t state_size = 1024 * 1024; // 1 MB
         auto db_map_mode = chainbase::pinnable_mapped_file::map_mode::mapped;

         database db( state_dir, database::read_write, state_size, false, db_map_mode );
         db.add_index<database_header_multi_index>();
         db.set_revision( 1 );
         uint32_t old_version = 0;
         db.create<database_header_object>([&](auto& header){
            old_version = header.version;
            header.version++;
         });
         db.commit(2);
      } FC_LOG_AND_RETHROW()
   }

   template<typename index_type>
   void print_last_undo(const index_type& idx) {

      auto undo = idx.last_undo_session();

      size_t num_old = std::distance(undo.old_values.begin(), undo.old_values.end());
      size_t num_old_change = std::count_if(undo.old_values.begin(), undo.old_values.end(),
                        [&idx](const auto& old) { return include_delta(old, idx.get(old.id)); });
      size_t num_rm = std::distance(undo.removed_values.begin(), undo.removed_values.end());
      size_t num_new = std::distance(undo.new_values.begin(), undo.new_values.end());

      idump((num_old)(num_old_change)(num_rm)(num_new));

      for (auto& old : undo.old_values) {
         wdump(("old")(old.id)(old));
         wdump(("old-curr")(idx.get(old.id)));
      }

      for (auto& rmv : undo.removed_values) {
         wdump((("rmv"))(rmv.id)(rmv));
         bool curr_existed = idx.find(rmv.id) != nullptr;
         wdump(("rmv-curr-existed")(curr_existed));
      }

      for (auto& newv : undo.new_values) {
         wdump(("newv")(newv.id)(newv));
         wdump(("newv-cur")(idx.get(newv.id)));
      }
   };

   // tests of database copying
   BOOST_AUTO_TEST_CASE(copy_db_test) {
      try {
         fc::temp_directory tempdir;
         auto state_dir = tempdir.path() / "state";
         wdump((state_dir));
         uint64_t state_size = 1024 * 1024; // 1 MB
         auto db_map_mode = chainbase::pinnable_mapped_file::map_mode::mapped;

         database db( state_dir, database::read_write, state_size, false, db_map_mode );
         db.add_index<account_metadata_index>();
         db.set_revision( 1 );
         const auto& idx = db.get_index<account_metadata_index>();
         BOOST_REQUIRE( !idx.has_undo_session() );
         BOOST_REQUIRE_EQUAL( idx.size(), 0 );

         auto sess0 = db.start_undo_session(true);
         BOOST_REQUIRE( idx.has_undo_session() );
         const auto& acct1 = db.create<account_metadata_object>([&](auto& obj){
            obj.name = "acct1"_n;
            obj.recv_sequence = 0;
         });
         BOOST_REQUIRE_EQUAL( idx.size(), 1 );


         auto sess1 = db.start_undo_session(true);
         db.modify( acct1, [&]( auto& obj ) {
            obj.recv_sequence++;
         });

         const auto& acct2 = db.create<account_metadata_object>([&](auto& obj){
            obj.name = "acct2"_n;
            obj.recv_sequence = 0;
         });
         BOOST_REQUIRE_EQUAL( idx.size(), 2 );

         const auto& acct3 = db.create<account_metadata_object>([&](auto& obj){
            obj.name = "acct3"_n;
            obj.recv_sequence = 0;
         });
         BOOST_REQUIRE_EQUAL( idx.size(), 3 );

         db.modify( acct2, [&]( auto& obj ) {
            obj.recv_sequence++;
         });

         print_last_undo(idx);

         auto ses2 = db.start_undo_session(true);

         const auto& acct4 = db.create<account_metadata_object>([&](auto& obj){
            obj.name = "acct4"_n;
            obj.recv_sequence = 0;
         });
         BOOST_REQUIRE_EQUAL( idx.size(), 4 );

         db.remove( acct2 );
         BOOST_REQUIRE_EQUAL( idx.size(), 3 );

         db.modify( acct3, [&]( auto& obj ) {
            obj.recv_sequence++;
         });

         const auto& acct2_1 = db.create<account_metadata_object>([&](auto& obj){
            obj.name = "acct2"_n;
            obj.recv_sequence = 3;
         });
         BOOST_REQUIRE_EQUAL( acct2_1.recv_sequence, 3 );
         BOOST_REQUIRE_EQUAL( idx.size(), 4 );
         db.modify( acct4, [&]( auto& obj ) {
            obj.recv_sequence++;
         });

         db.remove( acct1 );
         db.remove( acct3 );
         db.remove( acct4 );
         BOOST_REQUIRE_EQUAL( idx.size(), 1 );

         print_last_undo(idx);

         ses2.squash();

         print_last_undo(idx);

         db.commit(2);

      } FC_LOG_AND_RETHROW()
   }

   // Simple tests of undo infrastructure
   BOOST_AUTO_TEST_CASE(shared_db_test) {
      try {
         TESTER test;

         auto& control = *test.control;
         const auto& dbm = control.dbm();
         const auto& main_db = dbm.main_db();
         const auto& shared_db = dbm.shared_db();

         test.produce_blocks();
         wdump((control.head_block_num() ));

         const auto& main_acct_idx = main_db.get_index<account_index>();
         const auto& shared_acct_idx = shared_db.get_index<account_index>();
         // wdump((main_acct_idx.size()));
         // wdump((shared_acct_idx.size()));
         BOOST_REQUIRE_EQUAL( main_acct_idx.size(), shared_acct_idx.size() );
         BOOST_REQUIRE_EQUAL( shared_acct_idx.size(), 3 );

         using acct_idx_helper = index_helper<account_index>;
         // wdump(("main db")(acct_idx_helper::get_rows(main_db)));
         // wdump(("shared db")(acct_idx_helper::get_rows(shared_db)));
         auto main_accts = acct_idx_helper::get_rows(main_db);
         auto shared_accts = acct_idx_helper::get_rows(shared_db);
         // wdump((main_accts));
         // wdump((shared_accts));
         BOOST_REQUIRE( is_equal(main_accts, shared_accts) );

         BOOST_REQUIRE_EQUAL( shared_accts[0].name, config::system_account_name );
         BOOST_REQUIRE( bool(shared_db.find<account_object, by_name>(config::system_account_name)) );
         BOOST_REQUIRE_EQUAL( shared_accts[1].name, config::null_account_name );
         BOOST_REQUIRE( bool(shared_db.find<account_object, by_name>(config::null_account_name)) );
         BOOST_REQUIRE_EQUAL( shared_accts[2].name, config::producers_account_name );
         BOOST_REQUIRE( bool(shared_db.find<account_object, by_name>(config::producers_account_name)) );


         test.create_accounts( {"alice"_n, "bob"_n, "carol"_n} );
         test.produce_blocks();

         BOOST_REQUIRE_EQUAL( main_acct_idx.size(), shared_acct_idx.size() );
         BOOST_REQUIRE_EQUAL( shared_acct_idx.size(), 6 );
         main_accts = acct_idx_helper::get_rows(main_db);
         shared_accts = acct_idx_helper::get_rows(shared_db);
         BOOST_REQUIRE( is_equal(main_accts, shared_accts) );
         BOOST_REQUIRE_EQUAL( shared_accts[3].name, "alice"_n );
         BOOST_REQUIRE( bool(shared_db.find<account_object,by_name>("alice"_n)) );
         BOOST_REQUIRE_EQUAL( shared_accts[4].name, "bob"_n );
         BOOST_REQUIRE( bool(shared_db.find<account_object,by_name>("bob"_n)) );
         BOOST_REQUIRE_EQUAL( shared_accts[5].name, "carol"_n );
         BOOST_REQUIRE( bool(shared_db.find<account_object,by_name>("carol"_n)) );

      } FC_LOG_AND_RETHROW()
   }

BOOST_AUTO_TEST_SUITE_END()
