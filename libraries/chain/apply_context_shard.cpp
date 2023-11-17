#include <algorithm>
#include <eosio/chain/apply_context.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction_context.hpp>
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/wasm_interface.hpp>
#include <eosio/chain/generated_transaction_object.hpp>
#include <eosio/chain/authorization_manager.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/account_object.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/global_property_object.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <boost/container/flat_set.hpp>
#include <eosio/chain/database_manager.hpp>

using boost::container::flat_set;

namespace eosio { namespace chain {

template<typename object>
static inline uint64_t get_shared_billable_size() {
   return config::billable_size_v<object> * config::shared_contract_bytes_multiplier;
}

static inline uint64_t get_shared_billable_size(uint64_t data_size ) {
   return data_size * config::shared_contract_bytes_multiplier;
}

const table_id_object* apply_context::shared_find_table( name code, name scope, name table ) {
   return db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
}

const table_id_object& apply_context::shared_find_or_create_table( name code, name scope, name table, const account_name &payer ) {
   const auto* existing_tid =  db.find<table_id_object, by_code_scope_table>(boost::make_tuple(code, scope, table));
   if (existing_tid != nullptr) {
      return *existing_tid;
   }

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("shared:${code}:${scope}:${table}",
         ("code", code)
         ("scope", scope)
         ("table", table)
      );
      dm_logger->on_ram_trace(std::move(event_id), "table", "add", "create_table");
   }

   update_db_usage(payer, get_shared_billable_size<table_id_object>());

   return db.create<table_id_object>([&](table_id_object &t_id){
      t_id.code = code;
      t_id.scope = scope;
      t_id.table = table;
      t_id.payer = payer;

      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
         dm_logger->on_create_table(t_id);
      }
   });
}

void apply_context::shared_remove_table( const table_id_object& tid ) {
   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${code}:${scope}:${table}",
         ("code", tid.code)
         ("scope", tid.scope)
         ("table", tid.table)
      );
      dm_logger->on_ram_trace(std::move(event_id), "table", "remove", "remove_table");
   }

   update_db_usage(tid.payer, - get_shared_billable_size<table_id_object>());

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_remove_table(tid);
   }

   db.remove(tid);
}

int apply_context::shared_db_store_i64( name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot store a db record when executing a readonly transaction" );
   return db_store_i64( receiver, scope, table, payer, id, buffer, buffer_size);
}

int apply_context::shared_db_store_i64( name code, name scope, name table, const account_name& payer, uint64_t id, const char* buffer, size_t buffer_size ) {
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

   int64_t billable_size = (int64_t)(get_shared_billable_size(buffer_size) + get_shared_billable_size<key_value_object>());

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", tab.code)
         ("scope", tab.scope)
         ("table_name", tab.table)
         ("primkey", name(obj.primary_key))
      );
      dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_add");
   }

   update_db_usage( payer, billable_size);

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_db_store_i64(tab, obj);
   }

   keyval_cache.cache_table( tab );
   return keyval_cache.add( obj );
}

void apply_context::shared_db_update_i64( int iterator, account_name payer, const char* buffer, size_t buffer_size ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot update a db record when executing a readonly transaction" );
   const key_value_object& obj = keyval_cache.get( iterator );

   const auto& table_obj = keyval_cache.get_table( obj.t_id );
   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

//   require_write_lock( table_obj.scope );

   const int64_t overhead = get_shared_billable_size<key_value_object>();
   int64_t old_size = (int64_t)(get_shared_billable_size(obj.value.size()) + overhead);
   int64_t new_size = (int64_t)(get_shared_billable_size(buffer_size) + overhead);

   if( payer == account_name() ) payer = obj.payer;

   std::string event_id;
   if (control.get_deep_mind_logger(trx_context.is_transient()) != nullptr) {
      event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", table_obj.code)
         ("scope", table_obj.scope)
         ("table_name", table_obj.table)
         ("primkey", name(obj.primary_key))
      );
   }

   if( account_name(obj.payer) != payer ) {
      // refund the existing payer
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::string(event_id), "table_row", "remove", "primary_index_update_remove_old_payer");
      }
      update_db_usage( obj.payer,  -(old_size) );
      // charge the new payer
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::move(event_id), "table_row", "add", "primary_index_update_add_new_payer");
      }
      update_db_usage( payer,  (new_size));
   } else if(old_size != new_size) {
      // charge/refund the existing payer the difference
      if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient()))
      {
         dm_logger->on_ram_trace(std::move(event_id) , "table_row", "update", "primary_index_update");
      }
      update_db_usage( obj.payer, new_size - old_size);
   }

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      dm_logger->on_db_update_i64(table_obj, obj, payer, buffer, buffer_size);
   }

   db.modify( obj, [&]( auto& o ) {
     o.value.assign( buffer, buffer_size );
     o.payer = payer;
   });
}

void apply_context::shared_db_remove_i64( int iterator ) {
   EOS_ASSERT( !trx_context.is_read_only(), table_operation_not_permitted, "cannot remove a db record when executing a readonly transaction" );
   const key_value_object& obj = keyval_cache.get( iterator );

   const auto& table_obj = keyval_cache.get_table( obj.t_id );
   EOS_ASSERT( table_obj.code == receiver, table_access_violation, "db access violation" );

//   require_write_lock( table_obj.scope );

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
      std::string event_id = RAM_EVENT_ID("${table_code}:${scope}:${table_name}:${primkey}",
         ("table_code", table_obj.code)
         ("scope", table_obj.scope)
         ("table_name", table_obj.table)
         ("primkey", name(obj.primary_key))
      );
      dm_logger->on_ram_trace(std::move(event_id), "table_row", "remove", "primary_index_remove");
   }

   update_db_usage( obj.payer,  -(get_shared_billable_size(obj.value.size()) + get_shared_billable_size<key_value_object>()) );

   if (auto dm_logger = control.get_deep_mind_logger(trx_context.is_transient())) {
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

int apply_context::shared_db_get_i64( int iterator, char* buffer, size_t buffer_size ) {
   const key_value_object& obj = keyval_cache.get( iterator );

   auto s = obj.value.size();
   if( buffer_size == 0 ) return s;

   auto copy_size = std::min( buffer_size, s );
   memcpy( buffer, obj.value.data(), copy_size );

   return copy_size;
}

int apply_context::shared_db_next_i64( int iterator, uint64_t& primary ) {
   if( iterator < -1 ) return -1; // cannot increment past end iterator of table

   const auto& obj = keyval_cache.get( iterator ); // Check for iterator != -1 happens in this call
   const auto& idx = db.get_index<key_value_index, by_scope_primary>();

   auto itr = idx.iterator_to( obj );
   ++itr;

   if( itr == idx.end() || itr->t_id != obj.t_id ) return keyval_cache.get_end_iterator_by_table_id(obj.t_id);

   primary = itr->primary_key;
   return keyval_cache.add( *itr );
}

int apply_context::shared_db_previous_i64( int iterator, uint64_t& primary ) {
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

int apply_context::shared_db_find_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = shared_find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const key_value_object* obj = db.find<key_value_object, by_scope_primary>( boost::make_tuple( tab->id, id ) );
   if( !obj ) return table_end_itr;

   return keyval_cache.add( *obj );
}

int apply_context::shared_db_lowerbound_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = shared_find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
   auto itr = idx.lower_bound( boost::make_tuple( tab->id, id ) );
   if( itr == idx.end() ) return table_end_itr;
   if( itr->t_id != tab->id ) return table_end_itr;

   return keyval_cache.add( *itr );
}

int apply_context::shared_db_upperbound_i64( name code, name scope, name table, uint64_t id ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = shared_find_table( code, scope, table );
   if( !tab ) return -1;

   auto table_end_itr = keyval_cache.cache_table( *tab );

   const auto& idx = db.get_index<key_value_index, by_scope_primary>();
   auto itr = idx.upper_bound( boost::make_tuple( tab->id, id ) );
   if( itr == idx.end() ) return table_end_itr;
   if( itr->t_id != tab->id ) return table_end_itr;

   return keyval_cache.add( *itr );
}

int apply_context::shared_db_end_i64( name code, name scope, name table ) {
   //require_read_lock( code, scope ); // redundant?

   const auto* tab = shared_find_table( code, scope, table );
   if( !tab ) return -1;

   return keyval_cache.cache_table( *tab );
}

} } /// eosio::chain
