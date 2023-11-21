#include <eosio/chain/database_manager.hpp>
#include <chainbase/chainbase.hpp>
#include <fc/filesystem.hpp>

namespace eosio{ namespace testing {
   template<uint64_t MAX_SIZE>
   struct database_manager_fixture{
      database_manager_fixture()
      :_tempdir()
      ,_dbm(std::make_unique<eosio::chain::database_manager>(_tempdir.path(), chainbase::database::read_write, MAX_SIZE, MAX_SIZE, false, chainbase::pinnable_mapped_file::mapped))
      {}
      ~database_manager_fixture()
      {
          
      }
      
      fc::temp_directory                                _tempdir;
      std::unique_ptr<eosio::chain::database_manager>   _dbm;
   }; 
}}