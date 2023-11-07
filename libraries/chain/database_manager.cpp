#include <eosio/chain/database_manager.hpp>
#include <eosio/chain/config.hpp>
#include <boost/array.hpp>

#include <iostream>

#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace eosio { namespace chain {

      // namespace bip = boost::interprocess;
   // namespace bfs = boost::filesystem;
   // using std::unique_ptr;
   // using std::vector;

   // template<typename T>
   // using allocator = bip::allocator<T, pinnable_mapped_file::segment_manager>;

   // template<typename T>
   // using node_allocator = chainbase_node_allocator<T, pinnable_mapped_file::segment_manager>;

   // using shared_string = shared_cow_string;

   // typedef boost::interprocess::interprocess_sharable_mutex read_write_mutex;
   // typedef boost::interprocess::sharable_lock< read_write_mutex > read_lock;

   database_manager::database_manager(const database_manager::path& dir, open_flags flags,
                     uint64_t shared_file_size, uint64_t main_file_size, bool allow_dirty,
                     pinnable_mapped_file::map_mode db_map_mode) :
      _dir(dir),
      _flags(flags),
      _allow_dirty(allow_dirty),
      _db_map_mode(db_map_mode),
      _shared_db(dir / "shared", flags, shared_file_size, allow_dirty, db_map_mode),
      _main_db(dir / "main", flags, main_file_size, allow_dirty, db_map_mode),
      _read_only(flags == open_flags::read_only)
   {
      _read_only_mode = _read_only;
   }

   database_manager::~database_manager()
   {
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
            std::forward_as_tuple(_dir/name.to_string(), _flags, file_size, _allow_dirty, _db_map_mode) );
         itr = new_ret.first;
      }
      return &itr->second;
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
}}  // namespace eosio::chain
