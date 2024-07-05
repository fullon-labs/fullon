#pragma once

#include <eosio/trace_api/common.hpp>
#include <eosio/trace_api/trace.hpp>
#include <eosio/trace_api/extract_util.hpp>
#include <exception>
#include <functional>
#include <map>

namespace eosio { namespace trace_api {

using chain::transaction_id_type;
using chain::packed_transaction;

template <typename StoreProvider>
class chain_extraction_impl_type {
public:
   /**
    * Chain Extractor for capturing transaction traces, action traces, and block info.
    * @param store provider of append & append_lib
    * @param except_handler called on exceptions, logging if any is left to the user
    */
   chain_extraction_impl_type( StoreProvider store, exception_handler except_handler )
   : store(std::move(store))
   , except_handler(std::move(except_handler))
   {}

   /// connect to chain controller applied_transaction signal
   void signal_applied_transaction( const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& ptrx ) {
      on_applied_transaction( trace, ptrx );
   }

   /// connect to chain controller accepted_block signal
   void signal_accepted_block( const chain::block_state_ptr& bsp ) {
      on_accepted_block( bsp );
   }

   /// connect to chain controller irreversible_block signal
   void signal_irreversible_block( const chain::block_state_ptr& bsp ) {
      on_irreversible_block( bsp );
   }

   /// connect to chain controller block_start signal
   void signal_block_start( uint32_t block_num ) {
      on_block_start( block_num );
   }

private:

   void on_applied_transaction(const chain::transaction_trace_ptr& trace, const chain::packed_transaction_ptr& t) {
      if( !trace->receipt ) return;
      // include only executed transactions; soft_fail included so that onerror (and any inlines via onerror) are included
      if((trace->receipt->status != chain::transaction_receipt_header::executed &&
          trace->receipt->status != chain::transaction_receipt_header::soft_fail)) {
         return;
      }

      const auto& shard_name = t->get_shard_name();
      if (chain::is_onblock( *trace ) && shard_name == chain::config::main_shard_name) {
         onblock_trace.emplace( cache_trace{trace, t} );
      } else {
         auto& shard = init_shard(shard_name);
         if( trace->failed_dtrx_trace ) {
            shard[trace->failed_dtrx_trace->id] = {trace, t};
         } else {
            shard[trace->id] = {trace, t};
         }
      }
   }

   void on_accepted_block(const chain::block_state_ptr& block_state) {
      store_block_trace( block_state );
   }

   void on_irreversible_block( const chain::block_state_ptr& block_state ) {
      store_lib( block_state );
   }

   void on_block_start( uint32_t block_num ) {
      clear_caches();
   }

   void clear_caches() {
      shard_traces.clear();
      onblock_trace.reset();
   }

   void store_block_trace( const chain::block_state_ptr& block_state ) {
      try {
         using transaction_trace_t = transaction_trace_v3;
         auto bt = create_block_trace( block_state );

         const auto& receipts = block_state->block->transactions;

         std::vector<transaction_trace_t> traces;
         traces.reserve( receipts.size() + 1 );
         block_trxs_entry tt;
         tt.ids.reserve(receipts.size() + 1);

         if( onblock_trace )
            traces.emplace_back( to_transaction_trace<transaction_trace_t>( *onblock_trace ));

         auto shard_itr = shard_traces.end();
         for( const auto& r : receipts ) {
            auto const& id = r.get_trx_id();
            const auto& shard_name = r.get_shard_name();
            if (shard_itr == shard_traces.end() || shard_itr->first != shard_name) {
               shard_itr = shard_traces.find(shard_name);
            }

            if (shard_itr != shard_traces.end() ) {
               const auto& cached_trxs = shard_itr->second;
               auto trx_itr = cached_trxs.find(id);
               if( trx_itr != cached_trxs.end() ) {
                  traces.emplace_back( to_transaction_trace<transaction_trace_t>( trx_itr->second ));
               }
            }
            tt.ids.emplace_back(id);
         }
         bt.transactions = std::move( traces );
         clear_caches();

         // tt entry acts as a placeholder in a trx id slice if this block has no transaction
         tt.block_num = bt.number;
         store.append_trx_ids( std::move(tt) );

         store.append( std::move( bt ) );
      } catch( ... ) {
         except_handler( MAKE_EXCEPTION_WITH_CONTEXT( std::current_exception() ) );
      }
   }

   void store_lib( const chain::block_state_ptr& bsp ) {
      try {
         store.append_lib( bsp->block_num );
      } catch( ... ) {
         except_handler( MAKE_EXCEPTION_WITH_CONTEXT( std::current_exception() ) );
      }
   }

private:
   typedef std::map<transaction_id_type, cache_trace> transaction_trace_map;

   StoreProvider                                                store;
   exception_handler                                            except_handler;
   std::map<chain::shard_name, transaction_trace_map>           shard_traces;
   std::optional<cache_trace>                                   onblock_trace;
   std::mutex                                                   mtx;

   transaction_trace_map& init_shard(const chain::shard_name& name) {
      std::lock_guard g(mtx);
      return shard_traces[name];
   }
};

}}
