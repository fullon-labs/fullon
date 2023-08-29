#include <eosio/chain/database_manager.hpp>
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

   database_manager::database_manager(const database_manager::path& dir, open_flags flags, uint64_t shared_file_size, bool allow_dirty,
                      pinnable_mapped_file::map_mode db_map_mode) :
      _shared_db(dir / "shared", flags, shared_file_size, allow_dirty, db_map_mode),
      _main_db(dir / "main", flags, shared_file_size, allow_dirty, db_map_mode),
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
   }

   void database_manager::squash()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to squash in read-only mode" ) );
      _shared_db.squash();
      _main_db.squash();
   }

   void database_manager::commit( int64_t revision )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to commit in read-only mode" ) );
      _shared_db.commit( revision );
      _main_db.commit( revision );
   }

   void database_manager::undo_all()
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to undo_all in read-only mode" ) );

      _shared_db.undo_all();
      _main_db.undo_all();
   }

   database_manager::session database_manager::start_undo_session( bool enabled )
   {
      if ( _read_only_mode )
         BOOST_THROW_EXCEPTION( std::logic_error( "attempting to start_undo_session in read-only mode" ) );
      if( enabled ) {
         std::vector< std::unique_ptr<database::session> > _db_sessions;

         _db_sessions.reserve( 2 );
         _db_sessions.push_back(std::make_unique<database::session>(_shared_db.start_undo_session(enabled)));
         _db_sessions.push_back(std::make_unique<database::session>(_main_db.start_undo_session(enabled)));
         return session( std::move( _db_sessions ) );
      } else {
         return session();
      }
   }

}}  // namespace eosio::chain
