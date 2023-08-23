#pragma once

#include <chainbase/chainbase.hpp>

namespace eosio{ namespace chain {

   /**
    *  database manager
    */
   class database_manager
   {
      public:
         typedef chainbase::database               database;
         typedef chainbase::pinnable_mapped_file   pinnable_mapped_file;
         typedef boost::filesystem::path           path;
         typedef database::open_flags              open_flags;

         using database_index_row_count_multiset = std::multiset<std::pair<unsigned, std::string>>;

         database_manager(const path& dir, open_flags write = open_flags::read_only, uint64_t shared_file_size = 0, bool allow_dirty = false,
                  pinnable_mapped_file::map_mode = pinnable_mapped_file::map_mode::mapped);
         ~database_manager();
         database_manager(database_manager&&) = default;
         database_manager& operator=(database_manager&&) = default;
         bool is_read_only() const { return _read_only; }
         void flush();

         const database& shared_db() const { return _shared_db; }
         database& shared_db() { return _shared_db; }

         const database& main_db() const { return _main_db; }
         database& main_db() { return _main_db; }

         struct session_manager {
            public:
               session_manager( session_manager&& s ):_db_sessions( std::move(s._db_sessions) ){}
               session_manager( std::vector<std::unique_ptr<database::session>>&& s ):_db_sessions( std::move(s) )
               {
               }

               ~session_manager() {
                  undo();
               }

               void push()
               {
                  for( auto& i : _db_sessions ) i->push();
                  _db_sessions.clear();
               }

               void squash()
               {
                  for( auto& i : _db_sessions ) i->squash();
                  _db_sessions.clear();
               }

               void undo()
               {
                  for( auto& i : _db_sessions ) i->undo();
                  _db_sessions.clear();
               }

            private:
               friend class database_manager;
               session_manager(){}

               std::vector< std::unique_ptr<database::session> > _db_sessions;
         };

         session_manager start_undo_session( bool enabled );

         int64_t revision()const {
            return _main_db.revision();
         }

         void undo();
         void squash();
         void commit( int64_t revision );
         void undo_all();


         void set_revision( uint64_t revision )
         {
             if ( _read_only_mode ) {
                BOOST_THROW_EXCEPTION( std::logic_error( "attempting to set revision in read-only mode" ) );
             }
             _shared_db.set_revision(revision);
             _main_db.set_revision(revision);
            // TODO: shards db set_revision
            //  for( auto i : _index_list ) i->set_revision( revision );
         }

         template<typename MultiIndexType>
         void add_index() {
             _shared_db.add_index<MultiIndexType>();
             _main_db.add_index<MultiIndexType>();
         }

         void set_read_only_mode() {
            _read_only_mode = true;
            _shared_db.set_read_only_mode();
            _main_db.set_read_only_mode();
            // TODO: shard dbs
         }

         void unset_read_only_mode() {
             if ( _read_only )
                BOOST_THROW_EXCEPTION( std::logic_error( "attempting to unset read_only_mode while database was opened as read only" ) );
            _read_only_mode = false;
            _shared_db.unset_read_only_mode();
            _main_db.unset_read_only_mode();
            // TODO: shard dbs
         }

      private:
         database                                                    _shared_db;
         database                                                    _main_db;
         // TODO: map<database> _shard_db_map
         bool                                                        _read_only = false;

         /**
          * _read_only_mode is dynamic which can be toggled back and for
          * by users, while _read_only is static throughout the lifetime
          * of the database instance. When _read_only_mode is set to true,
          * an exception is thrown when modification attempt is made on
          * chainbase. This ensures state is not modified by mistake when
          * application does not intend to change state.
          */
         bool                                                        _read_only_mode = false;
   };

   template<typename Object, typename... Args>
   using shared_multi_index_container = boost::multi_index_container<Object,Args..., chainbase::node_allocator<Object> >;
}}  // namepsace chainbase
