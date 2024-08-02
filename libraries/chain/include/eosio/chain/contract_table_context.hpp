#pragma once
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <fc/utility.hpp>
#include <sstream>
#include <algorithm>
#include <set>

namespace chainbase { class database; }

namespace eosio { namespace chain {

class apply_context;
class controller;

template<typename Tables>
class contract_table_context_base {
   using table_id_object = typename Tables::table_id_object;
   using key_value_object = typename Tables::key_value_object;
   using index64_object = typename Tables::index64_object;
   using index128_object = typename Tables::index128_object;
   using index256_object = typename Tables::index256_object;
   using index_double_object = typename Tables::index_double_object;
   using index_long_double_object = typename Tables::index_long_double_object;
   using table_id_object_id_type = typename table_id_object::id_type;
   using key_value_index = typename chainbase::get_index_type<key_value_object>::type;
   public:
      template<typename T>
      class iterator_cache {
         public:
            iterator_cache(){
               _end_iterator_to_table.reserve(8);
               _iterator_to_object.reserve(32);
            }

            /// Returns end iterator of the table.
            int cache_table( const table_id_object& tobj ) {
               auto itr = _table_cache.find(tobj.id);
               if( itr != _table_cache.end() )
                  return itr->second.second;

               auto ei = index_to_end_iterator(_end_iterator_to_table.size());
               _end_iterator_to_table.push_back( &tobj );
               _table_cache.emplace( tobj.id, make_pair(&tobj, ei) );
               return ei;
            }

            const table_id_object& get_table( table_id_object_id_type i )const {
               auto itr = _table_cache.find(i);
               EOS_ASSERT( itr != _table_cache.end(), table_not_in_cache, "an invariant was broken, table should be in cache" );
               return *itr->second.first;
            }

            int get_end_iterator_by_table_id( table_id_object_id_type i )const {
               auto itr = _table_cache.find(i);
               EOS_ASSERT( itr != _table_cache.end(), table_not_in_cache, "an invariant was broken, table should be in cache" );
               return itr->second.second;
            }

            const table_id_object* find_table_by_end_iterator( int ei )const {
               EOS_ASSERT( ei < -1, invalid_table_iterator, "not an end iterator" );
               auto indx = end_iterator_to_index(ei);
               if( indx >= _end_iterator_to_table.size() ) return nullptr;
               return _end_iterator_to_table[indx];
            }

            const T& get( int iterator ) {
               EOS_ASSERT( iterator != -1, invalid_table_iterator, "invalid iterator" );
               EOS_ASSERT( iterator >= 0, table_operation_not_permitted, "dereference of end iterator" );
               EOS_ASSERT( (size_t)iterator < _iterator_to_object.size(), invalid_table_iterator, "iterator out of range" );
               auto result = _iterator_to_object[iterator];
               EOS_ASSERT( result, table_operation_not_permitted, "dereference of deleted object" );
               return *result;
            }

            void remove( int iterator ) {
               EOS_ASSERT( iterator != -1, invalid_table_iterator, "invalid iterator" );
               EOS_ASSERT( iterator >= 0, table_operation_not_permitted, "cannot call remove on end iterators" );
               EOS_ASSERT( (size_t)iterator < _iterator_to_object.size(), invalid_table_iterator, "iterator out of range" );

               auto obj_ptr = _iterator_to_object[iterator];
               if( !obj_ptr ) return;
               _iterator_to_object[iterator] = nullptr;
               _object_to_iterator.erase( obj_ptr );
            }

            int add( const T& obj ) {
               auto itr = _object_to_iterator.find( &obj );
               if( itr != _object_to_iterator.end() )
                    return itr->second;

               _iterator_to_object.push_back( &obj );
               _object_to_iterator[&obj] = _iterator_to_object.size() - 1;

               return _iterator_to_object.size() - 1;
            }

         private:
            map<table_id_object_id_type, pair<const table_id_object*, int>> _table_cache;
            vector<const table_id_object*>                  _end_iterator_to_table;
            vector<const T*>                                _iterator_to_object;
            map<const T*,int>                               _object_to_iterator;

            /// Precondition: std::numeric_limits<int>::min() < ei < -1
            /// Iterator of -1 is reserved for invalid iterators (i.e. when the appropriate table has not yet been created).
            inline size_t end_iterator_to_index( int ei )const { return (-ei - 2); }
            /// Precondition: indx < _end_iterator_to_table.size() <= std::numeric_limits<int>::max()
            inline int index_to_end_iterator( size_t indx )const { return -(indx + 2); }
      }; /// class iterator_cache

      template<typename>
      struct array_size;

      template<typename T, size_t N>
      struct array_size< std::array<T,N> > {
          static constexpr size_t size = N;
      };

      template <typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst, typename Enable = void>
      class secondary_key_helper;

      template<typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst>
      class secondary_key_helper<SecondaryKey, SecondaryKeyProxy, SecondaryKeyProxyConst,
         typename std::enable_if<std::is_same<SecondaryKey, typename std::decay<SecondaryKeyProxy>::type>::value>::type >
      {
         public:
            typedef SecondaryKey secondary_key_type;

            static void set(secondary_key_type& sk_in_table, const secondary_key_type& sk_from_wasm) {
               sk_in_table = sk_from_wasm;
            }

            static void get(secondary_key_type& sk_from_wasm, const secondary_key_type& sk_in_table ) {
               sk_from_wasm = sk_in_table;
            }

            static auto create_tuple(const table_id_object& tab, const secondary_key_type& secondary) {
               return boost::make_tuple( tab.id, secondary );
            }
      };

      template<typename SecondaryKey, typename SecondaryKeyProxy, typename SecondaryKeyProxyConst>
      class secondary_key_helper<SecondaryKey, SecondaryKeyProxy, SecondaryKeyProxyConst,
         typename std::enable_if<!std::is_same<SecondaryKey, typename std::decay<SecondaryKeyProxy>::type>::value &&
                                 std::is_pointer<typename std::decay<SecondaryKeyProxy>::type>::value>::type >
      {
         public:
            typedef SecondaryKey      secondary_key_type;
            typedef SecondaryKeyProxy secondary_key_proxy_type;
            typedef SecondaryKeyProxyConst secondary_key_proxy_const_type;

            static constexpr size_t N = array_size<SecondaryKey>::size;

            static void set(secondary_key_type& sk_in_table, secondary_key_proxy_const_type sk_from_wasm) {
               std::copy(sk_from_wasm, sk_from_wasm + N, sk_in_table.begin());
            }

            static void get(secondary_key_proxy_type sk_from_wasm, const secondary_key_type& sk_in_table) {
               std::copy(sk_in_table.begin(), sk_in_table.end(), sk_from_wasm);
            }

            static auto create_tuple(const table_id_object& tab, secondary_key_proxy_const_type sk_from_wasm) {
               secondary_key_type secondary;
               std::copy(sk_from_wasm, sk_from_wasm + N, secondary.begin());
               return boost::make_tuple( tab.id, secondary );
            }
      };

   public:
      template<typename ObjectType,
               typename SecondaryKeyProxy = typename std::add_lvalue_reference<typename ObjectType::secondary_key_type>::type,
               typename SecondaryKeyProxyConst = typename std::add_lvalue_reference<
                                                   typename std::add_const<typename ObjectType::secondary_key_type>::type>::type >
      class generic_index
      {
         public:
            typedef typename ObjectType::secondary_key_type secondary_key_type;
            typedef SecondaryKeyProxy      secondary_key_proxy_type;
            typedef SecondaryKeyProxyConst secondary_key_proxy_const_type;

            using secondary_key_helper_t = secondary_key_helper<secondary_key_type, secondary_key_proxy_type, secondary_key_proxy_const_type>;

            generic_index( contract_table_context_base& c ):context(c){}

            int store( uint64_t scope, uint64_t table, const account_name& payer,
                       uint64_t id, secondary_key_proxy_const_type value )
            {
               EOS_ASSERT( !context.trx_context.is_read_only(), table_operation_not_permitted, "cannot store a db record when executing a readonly transaction" );
               EOS_ASSERT( payer != account_name(), invalid_table_payer, "must specify a valid account to pay for new record" );

//               context.require_write_lock( scope );

               const auto& tab = context.find_or_create_table( context.receiver, name(scope), name(table), payer );

               const auto& obj = context.db.template create<ObjectType>( [&]( auto& o ){
                  o.t_id          = tab.id;
                  o.primary_key   = id;
                  secondary_key_helper_t::set(o.secondary_key, value);
                  o.payer         = payer;
               });

               context.db.template modify( tab, [&]( auto& t ) {
                 ++t.count;

                  if (auto dm_logger = context.control.get_deep_mind_logger(context.trx_context.is_transient())) {
                     std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}:${index_name}",
                        ("code", t.code)
                        ("scope", t.scope)
                        ("table", t.table)
                        ("index_name", name(id))
                     );
                     dm_logger->on_ram_trace(std::move(event_id), "secondary_index", "add", "secondary_index_add");
                  }
               });

               context.update_db_usage( payer, config::billable_size_v<ObjectType> );

               itr_cache.cache_table( tab );
               return itr_cache.add( obj );
            }

            void remove( int iterator ) {
               EOS_ASSERT( !context.trx_context.is_read_only(), table_operation_not_permitted, "cannot remove a db record when executing a readonly transaction" );
               const auto& obj = itr_cache.get( iterator );

               const auto& table_obj = itr_cache.get_table( obj.t_id );
               EOS_ASSERT( table_obj.code == context.receiver, table_access_violation, "db access violation" );

               if (auto dm_logger = context.control.get_deep_mind_logger(context.trx_context.is_transient())) {
                  std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}:${index_name}",
                     ("code", table_obj.code)
                     ("scope", table_obj.scope)
                     ("table", table_obj.table)
                     ("index_name", name(obj.primary_key))
                  );
                  dm_logger->on_ram_trace(std::move(event_id), "secondary_index", "remove", "secondary_index_remove");
               }

               context.update_db_usage( obj.payer, -( config::billable_size_v<ObjectType> ) );

//               context.require_write_lock( table_obj.scope );

               context.db.template modify( table_obj, [&]( auto& t ) {
                  --t.count;
               });
               context.db.template remove( obj );

               if (table_obj.count == 0) {
                  context.remove_table(table_obj);
               }

               itr_cache.remove( iterator );
            }

            void update( int iterator, account_name payer, secondary_key_proxy_const_type secondary ) {
               EOS_ASSERT( !context.trx_context.is_read_only(), table_operation_not_permitted, "cannot update a db record when executing a readonly transaction" );
               const auto& obj = itr_cache.get( iterator );

               const auto& table_obj = itr_cache.get_table( obj.t_id );
               EOS_ASSERT( table_obj.code == context.receiver, table_access_violation, "db access violation" );

//               context.require_write_lock( table_obj.scope );

               if( payer == account_name() ) payer = obj.payer;

               int64_t billing_size =  config::billable_size_v<ObjectType>;

               std::string event_id;
               if (context.control.get_deep_mind_logger(context.trx_context.is_transient()) != nullptr) {
                  event_id = RAM_EVENT_ID("${code}:${scope}:${table}:${index_name}",
                     ("code", table_obj.code)
                     ("scope", table_obj.scope)
                     ("table", table_obj.table)
                     ("index_name", name(obj.primary_key))
                  );
               }

               if( obj.payer != payer ) {
                  if (auto dm_logger = context.control.get_deep_mind_logger(context.trx_context.is_transient()))
                  {
                     dm_logger->on_ram_trace(std::string(event_id), "secondary_index", "remove", "secondary_index_remove");
                  }
                  context.update_db_usage( obj.payer, -(billing_size) );
                  if (auto dm_logger = context.control.get_deep_mind_logger(context.trx_context.is_transient()))
                  {
                     dm_logger->on_ram_trace(std::move(event_id), "secondary_index", "add", "secondary_index_update_add_new_payer");
                  }
                  context.update_db_usage( payer, +(billing_size) );
               }

               context.db.template modify( obj, [&]( auto& o ) {
                 secondary_key_helper_t::set(o.secondary_key, secondary);
                 o.payer = payer;
               });
            }

            int find_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_const_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto* obj = context.db.template find<ObjectType, by_secondary>( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( !obj ) return table_end_itr;

               primary = obj->primary_key;

               return itr_cache.add( *obj );
            }

            int lowerbound_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.template get_index< typename chainbase::get_index_type<ObjectType>::type, by_secondary >();
               auto itr = idx.lower_bound( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( itr == idx.end() ) return table_end_itr;
               if( itr->t_id != tab->id ) return table_end_itr;

               primary = itr->primary_key;
               secondary_key_helper_t::get(secondary, itr->secondary_key);

               return itr_cache.add( *itr );
            }

            int upperbound_secondary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t& primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.template get_index< typename chainbase::get_index_type<ObjectType>::type, by_secondary >();
               auto itr = idx.upper_bound( secondary_key_helper_t::create_tuple( *tab, secondary ) );
               if( itr == idx.end() ) return table_end_itr;
               if( itr->t_id != tab->id ) return table_end_itr;

               primary = itr->primary_key;
               secondary_key_helper_t::get(secondary, itr->secondary_key);

               return itr_cache.add( *itr );
            }

            int end_secondary( uint64_t code, uint64_t scope, uint64_t table ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if( !tab ) return -1;

               return itr_cache.cache_table( *tab );
            }

            int next_secondary( int iterator, uint64_t& primary ) {
               if( iterator < -1 ) return -1; // cannot increment past end iterator of index

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call
               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_secondary>();

               auto itr = idx.iterator_to(obj);
               ++itr;

               if( itr == idx.end() || itr->t_id != obj.t_id ) return itr_cache.get_end_iterator_by_table_id(obj.t_id);

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int previous_secondary( int iterator, uint64_t& primary ) {
               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_secondary>();

               if( iterator < -1 ) // is end iterator
               {
                  auto tab = itr_cache.find_table_by_end_iterator(iterator);
                  EOS_ASSERT( tab, invalid_table_iterator, "not a valid end iterator" );

                  auto itr = idx.upper_bound(tab->id);
                  if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty index

                  --itr;

                  if( itr->t_id != tab->id ) return -1; // Empty index

                  primary = itr->primary_key;
                  return itr_cache.add(*itr);
               }

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call

               auto itr = idx.iterator_to(obj);
               if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of index

               --itr;

               if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of index

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int find_primary( uint64_t code, uint64_t scope, uint64_t table, secondary_key_proxy_type secondary, uint64_t primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto* obj = context.db.template find<ObjectType, by_primary>( boost::make_tuple( tab->id, primary ) );
               if( !obj ) return table_end_itr;
               secondary_key_helper_t::get(secondary, obj->secondary_key);

               return itr_cache.add( *obj );
            }

            int lowerbound_primary( uint64_t code, uint64_t scope, uint64_t table, uint64_t primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if (!tab) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_primary>();
               auto itr = idx.lower_bound(boost::make_tuple(tab->id, primary));
               if (itr == idx.end()) return table_end_itr;
               if (itr->t_id != tab->id) return table_end_itr;

               return itr_cache.add(*itr);
            }

            int upperbound_primary( uint64_t code, uint64_t scope, uint64_t table, uint64_t primary ) {
               auto tab = context.find_table( name(code), name(scope), name(table) );
               if ( !tab ) return -1;

               auto table_end_itr = itr_cache.cache_table( *tab );

               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_primary>();
               auto itr = idx.upper_bound(boost::make_tuple(tab->id, primary));
               if (itr == idx.end()) return table_end_itr;
               if (itr->t_id != tab->id) return table_end_itr;

               itr_cache.cache_table(*tab);
               return itr_cache.add(*itr);
            }

            int next_primary( int iterator, uint64_t& primary ) {
               if( iterator < -1 ) return -1; // cannot increment past end iterator of table

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call
               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_primary>();

               auto itr = idx.iterator_to(obj);
               ++itr;

               if( itr == idx.end() || itr->t_id != obj.t_id ) return itr_cache.get_end_iterator_by_table_id(obj.t_id);

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            int previous_primary( int iterator, uint64_t& primary ) {
               const auto& idx = context.db.template get_index<typename chainbase::get_index_type<ObjectType>::type, by_primary>();

               if( iterator < -1 ) // is end iterator
               {
                  auto tab = itr_cache.find_table_by_end_iterator(iterator);
                  EOS_ASSERT( tab, invalid_table_iterator, "not a valid end iterator" );

                  auto itr = idx.upper_bound(tab->id);
                  if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty table

                  --itr;

                  if( itr->t_id != tab->id ) return -1; // Empty table

                  primary = itr->primary_key;
                  return itr_cache.add(*itr);
               }

               const auto& obj = itr_cache.get(iterator); // Check for iterator != -1 happens in this call

               auto itr = idx.iterator_to(obj);
               if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of table

               --itr;

               if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of index

               primary = itr->primary_key;
               return itr_cache.add(*itr);
            }

            void get( int iterator, uint64_t& primary, secondary_key_proxy_type secondary ) {
               const auto& obj = itr_cache.get( iterator );
               primary   = obj.primary_key;
               secondary_key_helper_t::get(secondary, obj.secondary_key);
            }

         private:
            contract_table_context_base&              context;
            iterator_cache<ObjectType>  itr_cache;
      }; /// class generic_index

   public:

      typedef iterator_cache<key_value_object>  key_value_cache;

      apply_context&                      context;
      transaction_context&                trx_context;
      controller&                         control;
      chainbase::database&                db;
      const account_name&                 receiver;

      key_value_cache                     keyval_cache;

      generic_index<index64_object>                                  idx64;
      generic_index<index128_object>                                 idx128;
      generic_index<index256_object, uint128_t*, const uint128_t*>   idx256;
      generic_index<index_double_object>                             idx_double;
      generic_index<index_long_double_object>                        idx_long_double;

      contract_table_context_base(apply_context& c, chainbase::database& db)
      :context(c)
      ,trx_context(c.trx_context)
      ,control(c.control)
      ,db(db)
      ,receiver(c.get_receiver())
      ,idx64(*this)
      ,idx128(*this)
      ,idx256(*this)
      ,idx_double(*this)
      ,idx_long_double(*this)
      {}

      int  db_store_i64( name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
         return db_store_i64( receiver, scope, table, payer, id, buffer, buffer_size);
      }

      int db_store_i64( name code, name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
      //   require_write_lock( scope );
         EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot store a db record when executing a readonly transaction" );
         const auto& tab = find_or_create_table( code, scope, table, payer );
         auto tableid = tab.id;

         EOS_ASSERT( payer != account_name(), invalid_table_payer, "must specify a valid account to pay for new record" );

         const auto& obj = db.create<key_value_object>( [&]( auto& o ) {
            o.t_id        = tableid;
            o.primary_key = id;
            o.value.assign( buffer, buffer_size );
            o.payer       = payer;
         });

         db.modify( tab, [&]( auto& t ) {
         ++t.count;
         });

         int64_t billable_size = (int64_t)(buffer_size + config::billable_size_v<key_value_object>);

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
               ("table_code", tab.code)
               ("scope", tab.scope)
               ("table_name", tab.table)
               ("primkey", name(obj.primary_key))
            );
            dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_add");
         }

         update_db_usage( payer, billable_size);

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            dm_logger->on_db_store_i64(tab, obj);
         }

         keyval_cache.cache_table( tab );
         return keyval_cache.add( obj );
      }

      void db_update_i64( int iterator, account_name payer, const char* buffer, size_t buffer_size ) {
         EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot update a db record when executing a readonly transaction" );
         const key_value_object& obj = keyval_cache.get( iterator );

         const auto& table_obj = keyval_cache.get_table( obj.t_id );
         EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

      //   require_write_lock( table_obj.scope );

         const int64_t overhead = config::billable_size_v<key_value_object>;
         int64_t old_size = (int64_t)(obj.value.size() + overhead);
         int64_t new_size = (int64_t)(buffer_size + overhead);

         if( payer == account_name() ) payer = obj.payer;

         std::string event_id;
         if (get_deep_mind_logger(trx_context.is_transient()) != nullptr) {
            event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
               ("table_code", table_obj.code)
               ("scope", table_obj.scope)
               ("table_name", table_obj.table)
               ("primkey", name(obj.primary_key))
            );
         }

         if( account_name(obj.payer) != payer ) {
            // refund the existing payer
            if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient()))
            {
               dm_logger->on_ram_trace(std::string(event_id), "table_row", "remove", "primary_index_update_remove_old_payer");
            }
            update_db_usage( obj.payer,  -(old_size) );
            // charge the new payer
            if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient()))
            {
               dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_update_add_new_payer");
            }
            update_db_usage( payer,  (new_size));
         } else if(old_size != new_size) {
            // charge/refund the existing payer the difference
            if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient()))
            {
               dm_logger->on_ram_trace(std::move(event_id) , "table_row", "update", "primary_index_update");
            }
            update_db_usage( obj.payer, new_size - old_size);
         }

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            dm_logger->on_db_update_i64(table_obj, obj, payer, buffer, buffer_size);
         }

         db.modify( obj, [&]( auto& o ) {
         o.value.assign( buffer, buffer_size );
         o.payer = payer;
         });
      }

      void db_remove_i64( int iterator ) {
         EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot remove a db record when executing a readonly transaction" );
         const key_value_object& obj = keyval_cache.get( iterator );

         const auto& table_obj = keyval_cache.get_table( obj.t_id );
         EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

      //   require_write_lock( table_obj.scope );

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
               ("table_code", table_obj.code)
               ("scope", table_obj.scope)
               ("table_name", table_obj.table)
               ("primkey", name(obj.primary_key))
            );
            dm_logger->on_ram_trace(std::move(event_id), "table_row", "remove", "primary_index_remove");
         }

         update_db_usage( obj.payer,  -(obj.value.size() + config::billable_size_v<key_value_object>) );

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            dm_logger->on_db_remove_i64(table_obj, obj);
         }

         db.modify( table_obj, [&]( auto& t ) {
            --t.count;
         });
         db.remove( obj );

         if (table_obj.count == 0) {
            remove_table(table_obj);
         }

         keyval_cache.remove( iterator );
      }

      int db_get_i64( int iterator, char* buffer, size_t buffer_size ) {
         const key_value_object& obj = keyval_cache.get( iterator );

         auto s = obj.value.size();
         if( buffer_size == 0 ) return s;

         auto copy_size = std::min( buffer_size, s );
         memcpy( buffer, obj.value.data(), copy_size );

         return copy_size;
      }

      int db_next_i64( int iterator, uint64_t& primary ) {
         if( iterator < -1 ) return -1; // cannot increment past end iterator of table

         const auto& obj = keyval_cache.get( iterator ); // Check for iterator != -1 happens in this call
         const auto& idx = db.get_index<key_value_index, by_scope_primary>();

         auto itr = idx.iterator_to( obj );
         ++itr;

         if( itr == idx.end() || itr->t_id != obj.t_id ) return keyval_cache.get_end_iterator_by_table_id(obj.t_id);

         primary = itr->primary_key;
         return keyval_cache.add( *itr );
      }

      int db_previous_i64( int iterator, uint64_t& primary ) {
         const auto& idx = db.get_index<key_value_index, by_scope_primary>();

         if( iterator < -1 ) // is end iterator
         {
            auto tab = keyval_cache.find_table_by_end_iterator(iterator);
            EOS_ASSERT( tab, invalid_table_iterator, "not a valid end iterator" );

            auto itr = idx.upper_bound(tab->id);
            if( idx.begin() == idx.end() || itr == idx.begin() ) return -1; // Empty table

            --itr;

            if( itr->t_id != tab->id ) return -1; // Empty table

            primary = itr->primary_key;
            return keyval_cache.add(*itr);
         }

         const auto& obj = keyval_cache.get(iterator); // Check for iterator != -1 happens in this call

         auto itr = idx.iterator_to(obj);
         if( itr == idx.begin() ) return -1; // cannot decrement past beginning iterator of table

         --itr;

         if( itr->t_id != obj.t_id ) return -1; // cannot decrement past beginning iterator of table

         primary = itr->primary_key;
         return keyval_cache.add(*itr);
      }

      int db_find_i64( name code, name scope, name table, uint64_t id ) {
         //require_read_lock( code, scope ); // redundant?

         const auto* tab = find_table( code, scope, table );
         if( !tab ) return -1;

         auto table_end_itr = keyval_cache.cache_table( *tab );

         const key_value_object* obj = db.find<key_value_object, by_scope_primary>( boost::make_tuple( tab->id, id ) );
         if( !obj ) return table_end_itr;

         return keyval_cache.add( *obj );
      }

      int db_lowerbound_i64( name code, name scope, name table, uint64_t id ) {
         //require_read_lock( code, scope ); // redundant?

         const auto* tab = find_table( code, scope, table );
         if( !tab ) return -1;

         auto table_end_itr = keyval_cache.cache_table( *tab );

         const auto& idx = db.get_index<key_value_index, by_scope_primary>();
         auto itr = idx.lower_bound( boost::make_tuple( tab->id, id ) );
         if( itr == idx.end() ) return table_end_itr;
         if( itr->t_id != tab->id ) return table_end_itr;

         return keyval_cache.add( *itr );
      }

      int db_upperbound_i64( name code, name scope, name table, uint64_t id ) {
         //require_read_lock( code, scope ); // redundant?

         const auto* tab = find_table( code, scope, table );
         if( !tab ) return -1;

         auto table_end_itr = keyval_cache.cache_table( *tab );

         const auto& idx = db.get_index<key_value_index, by_scope_primary>();
         auto itr = idx.upper_bound( boost::make_tuple( tab->id, id ) );
         if( itr == idx.end() ) return table_end_itr;
         if( itr->t_id != tab->id ) return table_end_itr;

         return keyval_cache.add( *itr );
      }

      int db_end_i64( name code, name scope, name table ) {
         //require_read_lock( code, scope ); // redundant?

         const auto* tab = find_table( code, scope, table );
         if( !tab ) return -1;

         return keyval_cache.cache_table( *tab );
      }


      void remove_table( const table_id_object& tid ) {
         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}",
               ("code", tid.code)
               ("scope", tid.scope)
               ("table", tid.table)
            );
            dm_logger->on_ram_trace(std::move(event_id), "table", "remove", "remove_table");
         }

         update_db_usage(tid.payer, - config::billable_size_v<table_id_object>);

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            dm_logger->on_remove_table(tid);
         }

         db.remove(tid);
      }

      inline deep_mind_handler* get_deep_mind_logger(bool is_trx_transient)const {
         return control.get_deep_mind_logger(is_trx_transient);
      }

      void update_db_usage( const account_name& payer, int64_t delta ) {
         context.update_db_usage(payer, delta);
      }

      const table_id_object* find_table( name code, name scope, name table ) {
         return db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
      }

      const table_id_object& find_or_create_table( name code, name scope, name table, const account_name &payer ) {
         const auto* existing_tid =  db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
         if (existing_tid != nullptr) {
            return *existing_tid;
         }

         if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
            std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}",
               ("code", code)
               ("scope", scope)
               ("table", table)
            );
            dm_logger->on_ram_trace(std::move(event_id), "table", "add", "create_table");
         }

         update_db_usage(payer, config::billable_size_v<table_id_object>);

         return db.create<table_id_object>([&](table_id_object &t_id){
            t_id.code = code;
            t_id.scope = scope;
            t_id.table = table;
            t_id.payer = payer;

            if (auto dm_logger = get_deep_mind_logger(trx_context.is_transient())) {
               dm_logger->on_create_table(t_id);
            }
         });
      }
};

} } // namespace eosio::chain

