#include <eosio/chain/database_manager.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/shard_object.hpp>
#include <boost/array.hpp>

#include <iostream>
#include <fc/io/fstream.hpp>

#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace eosio { namespace chain {


   const uint32_t shard_db_catalog::magic_number              = 0x30510FDB;
   const uint32_t shard_db_catalog::min_supported_version     = 1;
   const uint32_t shard_db_catalog::max_supported_version     = 1;

   database_manager::database_manager(const database_manager::path& dir, open_flags flags,
                     uint64_t shared_file_size, uint64_t main_file_size, bool allow_dirty,
                     pinnable_mapped_file::map_mode db_map_mode) :
      dir(dir),
      flags(flags),
      allow_dirty(allow_dirty),
      db_map_mode(db_map_mode),
      _shared_db(dir / config::share_db_name.to_string(), flags, shared_file_size, allow_dirty, db_map_mode),
      _main_db(dir / config::main_shard_name.to_string(), flags, main_file_size, allow_dirty, db_map_mode),
      _read_only(flags == open_flags::read_only)
   {
      _read_only_mode = _read_only;
   }

   database_manager::~database_manager()
   {
      if (_is_saving_catalog) {
         shard_db_catalog::save(*this);
         _is_saving_catalog = false;
      }
   }

   void database_manager::undo()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to undo in read-only mode" ) );
      _shared_db.undo();
      _main_db.undo();
      for ( auto& db : _shard_db_map ) {
         db.second.undo();
      }
   }

   void database_manager::squash()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to squash in read-only mode" ) );
      _shared_db.squash();
      _main_db.squash();
      for( auto& db: _shard_db_map ) {
         db.second.squash();
      }
   }

   void database_manager::commit( int64_t revision )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to commit in read-only mode" ) );
      _shared_db.commit( revision );
      _main_db.commit( revision );
      for ( auto& db : _shard_db_map ) {
         db.second.commit( revision );
      }
   }

   void database_manager::undo_all()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to undo_all in read-only mode" ) );

      _shared_db.undo_all();
      _main_db.undo_all();
      for ( auto& db : _shard_db_map ) {
         db.second.undo_all();
      }
   }

   database_manager::session database_manager::start_undo_session( bool enabled )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to start_undo_session in read-only mode" ) );
      if( enabled ) {
         std::vector< std::unique_ptr<database::session> > _db_sessions;
         _db_sessions.reserve( 2 + _shard_db_map.size() );
         _db_sessions.push_back(std::make_unique<database::session>(_shared_db.start_undo_session(enabled)));
         _db_sessions.push_back(std::make_unique<database::session>(_main_db.start_undo_session(enabled)));
         for( auto& db : _shard_db_map ) {
            _db_sessions.push_back(std::make_unique<database::session>(db.second.start_undo_session(enabled)));
         }
         return session( std::move( _db_sessions ) );
      } else {
         return session();
      }
   }

   database_manager::database* database_manager::add_shard_db( const shard_name& name, uint64_t file_size ) {
      auto itr = _shard_db_map.find(name);
      if (itr == _shard_db_map.end()) {
         // TODO: should add sub shard root dir 'dir/"shards"/name.to_string()'
         auto new_ret = _shard_db_map.emplace(std::piecewise_construct,std::forward_as_tuple(name),
            std::forward_as_tuple(dir / name.to_string(), flags, file_size, allow_dirty, db_map_mode) );
         itr = new_ret.first;
      }
      return &itr->second;
   }
   
   const database_manager::database& database_manager::shard_db(db_name shard_name) const {
      auto itr = _shard_db_map.find(shard_name);
      EOS_ASSERT(itr != _shard_db_map.end(), eosio::chain::database_exception,"${sname} db not found",("sname", shard_name));
      return itr->second;
   }
         
   database_manager::database& database_manager::shard_db( db_name shard_name) {
      auto itr = _shard_db_map.find(shard_name);
      EOS_ASSERT(itr != _shard_db_map.end(), eosio::chain::database_exception,"${sname} db not found",("sname", shard_name));
      return itr->second;
   }

   database_manager::database* database_manager::find_shard_db(const shard_name& name) {
      if (name == config::main_shard_name) {
         return &_main_db;
      } else {
         auto itr = _shard_db_map.find(name);
         if (itr != _shard_db_map.end()) {
            return &itr->second;
         }
      }
      return nullptr;
   }

   const database_manager::database* database_manager::find_shard_db(const shard_name& name) const {
      if (name == config::main_shard_name) {
         return &_main_db;
      } else {
         auto itr = _shard_db_map.find(name);
         if (itr != _shard_db_map.end()) {
            return &itr->second;
         }
      }
      return nullptr;
   }

   void shard_db_catalog::save(database_manager& dbm) {
      auto catalog_dat = dbm.dir / config::shard_db_catalog_filename;

      // TODO: backup the existed file?

      std::ofstream out( catalog_dat.generic_string().c_str(), std::ios::out | std::ios::binary | std::ofstream::trunc );
      fc::raw::pack( out, shard_db_catalog::magic_number );
      fc::raw::pack( out, shard_db_catalog::max_supported_version ); // write out current version which is always max_supported_version


      // TODO: shard_db_info {
      //    name, approved_block_num
      // }
      std::vector<shard_name> shards;
      const auto& shared_db = dbm.shared_db();
      std::string error_msg;

      const auto& s_indx = shared_db.get_index<shard_index, by_id>();
      for( auto itr = s_indx.begin(); itr != s_indx.end(); itr++ ) {
         // TODO: check if control->config (itr->name)
         auto db_ptr = dbm.find_shard_db(itr->name);
         if (!db_ptr) {
            error_msg = "The shard " + itr->name.to_string() + " is listened, but shard db does not exist";
            elog( error_msg );
            continue;
         }
         shards.push_back(itr->name);
      }

      fc::raw::pack( out, shards );
      fc::raw::pack( out, error_msg );
      // TODO: calc and pack check sum
   }

   shard_db_catalog shard_db_catalog::load(const fc::path& dir) {

      shard_db_catalog catalog;

      if (!fc::is_directory(dir))
         fc::create_directories(dir);

      auto catalog_dat = dir / config::shard_db_catalog_filename;
      if( !fc::exists( catalog_dat ) ) {
         return catalog;
      }

      try {
         string content;
         fc::read_file_contents( catalog_dat, content );

         fc::datastream<const char*> ds( content.data(), content.size() );

         // validate totem
         uint32_t totem = 0;
         fc::raw::unpack( ds, totem );
         EOS_ASSERT( totem == shard_db_catalog::magic_number, shard_db_catalog_exception,
                     "Shard db catalog file '${filename}' has unexpected magic number: ${actual_totem}. Expected ${expected_totem}",
                     ("filename", catalog_dat.generic_string())
                     ("actual_totem", totem)
                     ("expected_totem", shard_db_catalog::magic_number)
         );

         // validate version
         uint32_t version = 0;
         fc::raw::unpack( ds, version );
         EOS_ASSERT( version >= shard_db_catalog::min_supported_version && version <= shard_db_catalog::max_supported_version,
                     shard_db_catalog_exception,
                     "Unsupported version of shard db catalog file '${filename}'. "
                     "Shard db catalog version is ${version} while code supports version(s) [${min},${max}]",
                     ("filename", catalog_dat.generic_string())
                     ("version", version)
                     ("min", shard_db_catalog::min_supported_version)
                     ("max", shard_db_catalog::max_supported_version)
         );


         fc::raw::unpack( ds, catalog.shards );
         fc::raw::unpack( ds, catalog.error_msg );

         if (!catalog.error_msg.empty()) {
            EOS_ASSERT( totem == shard_db_catalog::magic_number, shard_db_catalog_exception,
                        "Shard db catalog file ${filename} has been broken before saving, error_msg: ${e}",
                        ("filename", catalog_dat.generic_string())
                        ("e", catalog.error_msg)
            );
         }
      } FC_CAPTURE_AND_RETHROW( (catalog_dat) )

      return catalog;
   }

}}  // namespace eosio::chain
