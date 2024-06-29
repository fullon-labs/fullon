#pragma once

#include <eosio/chain/block_state.hpp>
#include <eosio/state_history/types.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>

namespace eosio {
namespace state_history {

using chain::block_state_ptr;
using chain::transaction_id_type;

struct trace_converter {
   typedef std::map<transaction_id_type, augmented_transaction_trace> transaction_trace_map;

   std::map<chain::shard_name, transaction_trace_map>                shard_traces;
   std::optional<augmented_transaction_trace>                 onblock_trace;
   std::mutex                                                 mtx;

   transaction_trace_map& init_shard(const chain::shard_name& name);
   void clear();
   void add_transaction(const transaction_trace_ptr& trace, const chain::packed_transaction_ptr& transaction);
   void pack(boost::iostreams::filtering_ostreambuf& ds, const chainbase::database& db, bool trace_debug_mode, const block_state_ptr& block_state);
};

} // namespace state_history
} // namespace eosio
