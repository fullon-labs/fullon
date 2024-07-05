#include <eosio/state_history/serialization.hpp>
#include <eosio/state_history/trace_converter.hpp>

namespace eosio {
namespace state_history {

trace_converter::transaction_trace_map& trace_converter::init_shard(const chain::shard_name& name) {
   std::lock_guard g(mtx);
   return shard_traces[name];
}

void trace_converter::clear() {
   shard_traces.clear();
   onblock_trace.reset();
}

void trace_converter::add_transaction(const transaction_trace_ptr& trace, const chain::packed_transaction_ptr& transaction) {

   if (trace->receipt) {
      const auto& shard_name = transaction->get_shard_name();
      if (chain::is_onblock(*trace) && shard_name == chain::config::main_shard_name) {
         onblock_trace.emplace(trace, transaction);
      } else {
         auto& shard = init_shard(shard_name);
         if (trace->failed_dtrx_trace) {
            shard[trace->failed_dtrx_trace->id] = augmented_transaction_trace{trace, transaction};
         } else {
            shard[trace->id] = augmented_transaction_trace{trace, transaction};
         }
      }
   }
}

void trace_converter::pack(boost::iostreams::filtering_ostreambuf& obuf, const chainbase::database& db, bool trace_debug_mode, const block_state_ptr& block_state) {
   std::vector<augmented_transaction_trace> traces;
   traces.reserve( block_state->block->transactions.size() + 1 );

   if (onblock_trace)
      traces.push_back(*onblock_trace);

   auto shard_itr = shard_traces.end();
   for (auto& r : block_state->block->transactions) {
      const auto& id = r.get_trx_id();
      const auto& shard_name = r.get_shard_name();
      if (shard_itr == shard_traces.end() || shard_itr->first != shard_name) {
         shard_itr = shard_traces.find(shard_name);
         EOS_ASSERT(shard_itr != shard_traces.end(), chain::plugin_exception,
                  "missing trace for transaction shard ${s}", ("s", shard_name));
      }

      const auto& cached_trxs = shard_itr->second;
      auto trx_itr = cached_trxs.find(id);
      EOS_ASSERT(trx_itr != cached_trxs.end() && trx_itr->second.trace->receipt, chain::plugin_exception,
               "missing trace for transaction ${id} of shard {s}", ("id", id)("s", shard_name));
      traces.push_back(trx_itr->second);
   }
   clear();

   fc::datastream<boost::iostreams::filtering_ostreambuf&> ds{obuf};
   return fc::raw::pack(ds, make_history_context_wrapper(db, trace_debug_mode, traces));
}

} // namespace state_history
} // namespace eosio
