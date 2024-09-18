#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/resource_limits.hpp>
#include <eosio/chain/resource_limits_private.hpp>
#include <eosio/chain/transaction_metadata.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <eosio/chain/database_utils.hpp>
#include <algorithm>

namespace eosio { namespace chain { namespace resource_limits {

using resource_index_set = index_set<
   resource_limits_index,
   resource_usage_index,
   resource_limits_state_index,
   resource_limits_config_index
>;

using resource_shared_index_set = index_set<
   resource_limits_index,
   resource_limits_state_index,
   resource_limits_config_index
>;
static_assert( config::rate_limiting_precision > 0, "config::rate_limiting_precision must be positive" );

static uint64_t update_elastic_limit(uint64_t current_limit, uint64_t average_usage, const elastic_limit_parameters& params) {
   uint64_t result = current_limit;
   if (average_usage > params.target ) {
      result = result * params.contract_rate;
   } else {
      result = result * params.expand_rate;
   }
   return std::min(std::max(result, params.max), params.max * params.max_multiplier);
}

void elastic_limit_parameters::validate()const {
   // At the very least ensure parameters are not set to values that will cause divide by zero errors later on.
   // Stricter checks for sensible values can be added later.
   EOS_ASSERT( periods > 0, resource_limit_exception, "elastic limit parameter 'periods' cannot be zero" );
   EOS_ASSERT( contract_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'contract_rate' is not a well-defined ratio" );
   EOS_ASSERT( expand_rate.denominator > 0, resource_limit_exception, "elastic limit parameter 'expand_rate' is not a well-defined ratio" );
}


void resource_limits_state_object::update_virtual_cpu_limit( const resource_limits_config_object& cfg ) {
   //idump((average_block_cpu_usage.average()));
   virtual_cpu_limit = update_elastic_limit(virtual_cpu_limit, average_block_cpu_usage.average(), cfg.cpu_limit_parameters);
   //idump((virtual_cpu_limit));
}

void resource_limits_state_object::update_virtual_net_limit( const resource_limits_config_object& cfg ) {
   virtual_net_limit = update_elastic_limit(virtual_net_limit, average_block_net_usage.average(), cfg.net_limit_parameters);
}

void resource_limits_manager::add_indices(chainbase::database& db) {
   resource_index_set::add_indices(db);
}

void resource_limits_manager::add_shared_indices(chainbase::database& shared_db) {
   resource_shared_index_set::add_indices(shared_db);
}

void resource_limits_manager::copy_data(chainbase::database& main_db, chainbase::database& shared_db) {
   resource_shared_index_set::copy_data(main_db, shared_db);
}

void resource_limits_manager::copy_changes(chainbase::database& main_db, chainbase::database& shared_db) {
   resource_shared_index_set::copy_changes(main_db, shared_db);
}

void resource_limits_manager::initialize_database() {
   const auto& config = _dbm.main_db().create<resource_limits_config_object>([](resource_limits_config_object& config){
      // see default settings in the declaration
   });

   const auto& state = _dbm.main_db().create<resource_limits_state_object>([&config](resource_limits_state_object& state){
      // see default settings in the declaration

      // start the chain off in a way that it is "congested" aka slow-start
      state.virtual_cpu_limit = config.cpu_limit_parameters.max;
      state.virtual_net_limit = config.net_limit_parameters.max;
   });

   // At startup, no transaction specific logging is possible
   if (auto dm_logger = _get_deep_mind_logger(false)) {
      dm_logger->on_init_resource_limits(config, state);
   }
}

void resource_limits_manager::add_to_snapshot( chainbase::database& db, const snapshot_writer_ptr& snapshot ) {
   resource_index_set::walk_indices([&db, &snapshot]( auto utils ){
      snapshot->write_section<typename decltype(utils)::index_t::value_type>([&db]( auto& section ){
         decltype(utils)::walk(db, [&section, &db]( const auto &row ) {
            section.add_row(row, db);
         });
      });
   });
}
//TODO: subshard snapshot resource process.
void resource_limits_manager::read_from_snapshot( const snapshot_reader_ptr& snapshot ) {
   resource_index_set::walk_indices([this, &snapshot]( auto utils ){
      snapshot->read_section<typename decltype(utils)::index_t::value_type>([this]( auto& section ) {
         bool more = !section.empty();
         auto& db = _dbm.main_db();
         while(more) {
            decltype(utils)::create(db, [&section, &more, &db]( auto &row ) {
               more = section.read_row(row, db);
            });
         }
      });
   });
}

void resource_limits_manager::initialize_account(const account_name& account, bool is_trx_transient) {
   const auto& limits = _dbm.main_db().create<resource_limits_object>([&]( resource_limits_object& bl ) {
      bl.owner = account;
   });
   //resource_limits_state_index,
   //resource_limits_config_index

   const auto& usage = _dbm.main_db().create<resource_usage_object>([&]( resource_usage_object& bu ) {
         bu.owner = account;
   });

   if (auto dm_logger = _get_deep_mind_logger(is_trx_transient)) {
      dm_logger->on_newaccount_resource_limits(limits, usage);
   }
}

void resource_limits_manager::set_block_parameters(const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters ) {
   cpu_limit_parameters.validate();
   net_limit_parameters.validate();
   const auto& config = _dbm.main_db().get<resource_limits_config_object>();
   if( config.cpu_limit_parameters == cpu_limit_parameters && config.net_limit_parameters == net_limit_parameters )
      return;
   _dbm.main_db().modify(config, [&](resource_limits_config_object& c){
      c.cpu_limit_parameters = cpu_limit_parameters;
      c.net_limit_parameters = net_limit_parameters;

      // set_block_parameters is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_config(c);
      }
   });
}

void resource_limits_manager::update_account_usage(const flat_set<account_name>& accounts, uint32_t time_slot, chainbase::database& db, chainbase::database& shared_db ) {
   const auto& config = shared_db.get<resource_limits_config_object>();
   for( const auto& a : accounts ) {
      const auto& usage = get_or_create_usage_object(a, db);

      db.modify( usage, [&]( auto& bu ){
         bu.net_usage.add( 0, time_slot, config.account_net_usage_average_window );
         bu.cpu_usage.add( 0, time_slot, config.account_cpu_usage_average_window );
      });

   }
}

void resource_limits_manager::add_transaction_usage(const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t time_slot, chainbase::database& db, chainbase::database& shared_db, bool is_trx_transient ) {
   const auto& state = shared_db.get<resource_limits_state_object>();
   const auto& config = shared_db.get<resource_limits_config_object>();
   const auto* shard_state =  db.find<resource_limits_state_object>();
   EOS_ASSERT(shard_state,eosio::chain::shard_exception, "resource_limits_state_object not found on shard");

   for( const auto& a : accounts ) {

      const auto& usage = get_or_create_usage_object( a, db );
      int64_t unused;
      int64_t net_weight;
      int64_t cpu_weight;
      get_account_limits( a, unused, net_weight, cpu_weight, shared_db );

      db.modify( usage, [&]( auto& bu ){
         bu.net_usage.add( net_usage, time_slot, config.account_net_usage_average_window );
         bu.cpu_usage.add( cpu_usage, time_slot, config.account_cpu_usage_average_window );

         if (auto dm_logger = _get_deep_mind_logger(is_trx_transient)) {
            dm_logger->on_update_account_usage(bu);
         }
      });

      if( cpu_weight >= 0 && state.total_cpu_weight > 0 ) {
         uint128_t window_size = config.account_cpu_usage_average_window;
         auto virtual_network_capacity_in_window = (uint128_t)shard_state->virtual_cpu_limit * window_size;
         auto cpu_used_in_window                 = ((uint128_t)usage.cpu_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)cpu_weight;
         uint128_t all_user_weight = state.total_cpu_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( cpu_used_in_window <= max_user_use_in_window,
                     tx_cpu_usage_exceeded,
                     "authorizing account '${n}' has insufficient objective cpu resources for this transaction,"
                     " used in window ${cpu_used_in_window}us, allowed in window ${max_user_use_in_window}us",
                     ("n", a)
                     ("cpu_used_in_window",cpu_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );
      }

      if( net_weight >= 0 && state.total_net_weight > 0) {

         uint128_t window_size = config.account_net_usage_average_window;
         //${virtual_net_limit} was updated in process_block_usage(), which is directly related to ${_block_pending_net_usage}.
         auto virtual_network_capacity_in_window = (uint128_t)state.virtual_net_limit * window_size;
         auto net_used_in_window                 = ((uint128_t)usage.net_usage.value_ex * window_size) / (uint128_t)config::rate_limiting_precision;

         uint128_t user_weight     = (uint128_t)net_weight;
         uint128_t all_user_weight = state.total_net_weight;

         auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;

         EOS_ASSERT( net_used_in_window <= max_user_use_in_window,
                     tx_net_usage_exceeded,
                     "authorizing account '${n}' has insufficient net resources for this transaction,"
                     " used in window ${net_used_in_window}, allowed in window ${max_user_use_in_window}",
                     ("n", a)
                     ("net_used_in_window",net_used_in_window)
                     ("max_user_use_in_window",max_user_use_in_window) );

      }
   }

   // account for this transaction in the block and do not exceed those limits either
   // Conflicts arise from transaction parallelism and block level restrictions
   // shared_db.modify(state, [&](resource_limits_state_object& rls){
   //    rls.pending_cpu_usage += cpu_usage;
   //    rls.pending_net_usage += net_usage;
   // });

   db.modify( *shard_state, [&](resource_limits_state_object& rls){
      rls.pending_cpu_usage += cpu_usage;
      //instead of using this in shard db, using ${block_pending_net_usage}.
      //rls.pending_net_usage += net_usage; don't use this, use ${block_pending_net_usage}
   });

   //Thread safe

   add_block_pending_net(net_usage);
   EOS_ASSERT( get_block_pending_net() <= config.net_limit_parameters.max, block_resource_exhausted, "Block has insufficient net resources" );

   EOS_ASSERT( shard_state->pending_cpu_usage <= config.cpu_limit_parameters.max, block_resource_exhausted, "Block has insufficient cpu resources" );
}

void resource_limits_manager::add_pending_ram_usage( const account_name account, int64_t ram_delta, chainbase::database& db, bool is_trx_transient ) {
   if (ram_delta == 0) {
      return;
   }

   const auto& usage  = get_or_create_usage_object( account, db );
   EOS_ASSERT( ram_delta <= 0 || UINT64_MAX - usage.ram_usage >= (uint64_t)ram_delta, transaction_exception,
              "Ram usage delta would overflow UINT64_MAX");
   EOS_ASSERT(ram_delta >= 0 || (usage.ram_usage) >= (uint64_t)(-ram_delta), transaction_exception,
              "Ram usage delta would underflow UINT64_MAX");

   db.modify( usage, [&]( auto& u ) {
      u.ram_usage += ram_delta;

      if (auto dm_logger = _get_deep_mind_logger(is_trx_transient)) {
         dm_logger->on_ram_event(account, u.ram_usage, ram_delta);
      }
   });
}

void resource_limits_manager::verify_account_ram_usage( const account_name account, chainbase::database& db, chainbase::database& shared_db )const {
   int64_t ram_bytes; int64_t net_weight; int64_t cpu_weight;
   get_account_limits( account, ram_bytes, net_weight, cpu_weight, shared_db );
   const auto* usage  = db.find<resource_usage_object, by_owner>( account );
   EOS_ASSERT( usage != nullptr, transaction_exception, "Account resource usage object not found");
   if( ram_bytes >= 0 ) {
      EOS_ASSERT( usage->ram_usage <= static_cast<uint64_t>(ram_bytes), ram_usage_exceeded,
                  "account ${account} has insufficient ram; needs ${needs} bytes has ${available} bytes",
                  ("account", account)("needs",usage->ram_usage)("available", ram_bytes)              );
   }
}

int64_t resource_limits_manager::get_account_ram_usage( const account_name& name ) const {
   return get_account_ram_usage(name, _dbm.main_db());
}

int64_t resource_limits_manager::get_account_ram_usage( const account_name& name, chainbase::database& db )const{
   const auto* usage  = db.find<resource_usage_object,by_owner>( name );
   if( usage != nullptr ) {
      return usage->ram_usage;
   }
   return 0;
}

void resource_limits_manager::ensure_resource_limits_state_object(chainbase::database& db, const chainbase::database& shared_db) const {
   const auto& config = shared_db.get<resource_limits_config_object>();
   const auto* shard_state = db.find<resource_limits_state_object>();
   if( shard_state == nullptr ){
      db.create<resource_limits_state_object>([&config](resource_limits_state_object& rls){
         //start the shard in a way that it is "congested" aka slow-start too.
         rls.virtual_cpu_limit = config.cpu_limit_parameters.max;
         //rls.virtual_net_limit = config.net_limit_parameters.max;
      });
   }
}


bool resource_limits_manager::set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight,  chainbase::database& shared_db, bool is_trx_transient) {
   //const auto& usage = _db.get<resource_usage_object,by_owner>( account );
   /*
    * Since we need to delay these until the next resource limiting boundary, these are created in a "pending"
    * state or adjusted in an existing "pending" state.  The chain controller will collapse "pending" state into
    * the actual state at the next appropriate boundary.
    */
   auto find_or_create_pending_limits = [&]() -> const resource_limits_object& {
      const auto* pending_limits = shared_db.find<resource_limits_object, by_owner>( boost::make_tuple(true, account) );
      if (pending_limits == nullptr) {
         const auto& limits = shared_db.get<resource_limits_object, by_owner>( boost::make_tuple(false, account));
         return shared_db.create<resource_limits_object>([&](resource_limits_object& pending_limits){
            pending_limits.owner = limits.owner;
            pending_limits.ram_bytes = limits.ram_bytes;
            pending_limits.net_weight = limits.net_weight;
            pending_limits.cpu_weight = limits.cpu_weight;
            pending_limits.pending = true;
         });
      } else {
         return *pending_limits;
      }
   };

   // update the users weights directly
   auto& limits = find_or_create_pending_limits();

   bool decreased_limit = false;

   if( ram_bytes >= 0 ) {

      decreased_limit = ( (limits.ram_bytes < 0) || (ram_bytes < limits.ram_bytes) );

      /*
      if( limits.ram_bytes < 0 ) {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "converting unlimited account would result in overcommitment [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      } else {
         EOS_ASSERT(ram_bytes >= usage.ram_usage, wasm_execution_error, "attempting to release committed ram resources [commit=${c}, desired limit=${l}]", ("c", usage.ram_usage)("l", ram_bytes));
      }
      */
   }

   shared_db.modify( limits, [&]( resource_limits_object& pending_limits ){
      pending_limits.ram_bytes = ram_bytes;
      pending_limits.net_weight = net_weight;
      pending_limits.cpu_weight = cpu_weight;

      if (auto dm_logger = _get_deep_mind_logger(is_trx_transient)) {
         dm_logger->on_set_account_limits(pending_limits);
      }
   });

   return decreased_limit;
}


void resource_limits_manager::get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight ) const {
   get_account_limits( account, ram_bytes, net_weight, cpu_weight, _dbm.shared_db());
}

void resource_limits_manager::get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight, const chainbase::database& shared_db ) const {
   const auto* pending_buo = shared_db.find<resource_limits_object,by_owner>( boost::make_tuple(true, account) );
   if (pending_buo) {
      ram_bytes  = pending_buo->ram_bytes;
      net_weight = pending_buo->net_weight;
      cpu_weight = pending_buo->cpu_weight;
   } else {
      const auto& buo = shared_db.get<resource_limits_object,by_owner>( boost::make_tuple( false, account ) );
      ram_bytes  = buo.ram_bytes;
      net_weight = buo.net_weight;
      cpu_weight = buo.cpu_weight;
   }
}

bool resource_limits_manager::is_unlimited_cpu( const account_name& account, const chainbase::database& shared_db ) const {
   const auto* buo = shared_db.find<resource_limits_object,by_owner>( boost::make_tuple(false, account) );
   if (buo) {
      return buo->cpu_weight == -1;
   }
   return false;
}

void resource_limits_manager::process_account_limit_updates() {
   auto& db         = _dbm.main_db(); //main thread.
   auto& multi_index = db.get_mutable_index<resource_limits_index>();
   auto& by_owner_index = multi_index.indices().get<by_owner>();

   // convenience local lambda to reduce clutter
   auto update_state_and_value = [](uint64_t &total, int64_t &value, int64_t pending_value, const char* debug_which) -> void {
      if (value > 0) {
         EOS_ASSERT(total >= static_cast<uint64_t>(value), rate_limiting_state_inconsistent, "underflow when reverting old value to ${which}", ("which", debug_which));
         total -= value;
      }

      if (pending_value > 0) {
         EOS_ASSERT(UINT64_MAX - total >= static_cast<uint64_t>(pending_value), rate_limiting_state_inconsistent, "overflow when applying new value to ${which}", ("which", debug_which));
         total += pending_value;
      }

      value = pending_value;
   };

   const auto& state = db.get<resource_limits_state_object>();
   db.modify(state, [&](resource_limits_state_object& rso){
      while(!by_owner_index.empty()) {
         const auto& itr = by_owner_index.lower_bound(boost::make_tuple(true));
         if (itr == by_owner_index.end() || itr->pending!= true) {
            break;
         }

         const auto& actual_entry = db.get<resource_limits_object, by_owner>(boost::make_tuple(false, itr->owner));
         db.modify(actual_entry, [&](resource_limits_object& rlo){
            update_state_and_value(rso.total_ram_bytes,  rlo.ram_bytes,  itr->ram_bytes, "ram_bytes");
            update_state_and_value(rso.total_cpu_weight, rlo.cpu_weight, itr->cpu_weight, "cpu_weight");
            update_state_and_value(rso.total_net_weight, rlo.net_weight, itr->net_weight, "net_weight");
         });

         multi_index.remove(*itr);
      }

      // process_account_limit_updates is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_state(state);
      }
   });
}

void resource_limits_manager::process_block_usage(uint32_t block_num, std::vector<chainbase::database*> processing_shard) {
   auto&     db = _dbm.main_db();
   const auto& s = db.get<resource_limits_state_object>();
   const auto& config = db.get<resource_limits_config_object>();
   db.modify(s, [&](resource_limits_state_object& state){
      // apply pending usage, update virtual limits and reset the pending

      state.average_block_cpu_usage.add(state.pending_cpu_usage, block_num, config.cpu_limit_parameters.periods);
      state.update_virtual_cpu_limit(config);
      state.pending_cpu_usage = 0;

      state.average_block_net_usage.add( get_block_pending_net(), block_num, config.net_limit_parameters.periods);
      state.update_virtual_net_limit(config);
      state.pending_net_usage = 0;

      // process_block_usage is called by controller::finalize_block,
      // where transaction specific logging is not possible
      if (auto dm_logger = _get_deep_mind_logger(false)) {
         dm_logger->on_update_resource_limits_state(state);
      }
   });

   for(auto& sdb : processing_shard ){
      const auto* ss = sdb->find<resource_limits_state_object>();

      //Statistics of CPU bandwidth usage on shards executed by nodes
      sdb->modify( *ss, [&](resource_limits_state_object& shard_state){
         // apply pending usage, update virtual limits and reset the pending
         // management of sharded CPU bandwidth is performed on shard.
         shard_state.average_block_cpu_usage.add(shard_state.pending_cpu_usage, block_num, config.cpu_limit_parameters.periods);
         shard_state.update_virtual_cpu_limit(config);
         shard_state.pending_cpu_usage = 0;

         // process_block_usage is called by controller::finalize_block,
         // where transaction specific logging is not possible
         if (auto dm_logger = _get_deep_mind_logger(false)) {
            dm_logger->on_update_resource_limits_state(shard_state);
         }
      });
   }

}

uint64_t resource_limits_manager::get_total_cpu_weight() const {
   //TODO:
   auto& db = _dbm.main_db();
   const auto& state = db.get<resource_limits_state_object>();
   return state.total_cpu_weight;
}

uint64_t resource_limits_manager::get_total_net_weight() const {
   //TODO:
   auto& db = _dbm.main_db();
   const auto& state = db.get<resource_limits_state_object>();
   return state.total_net_weight;
}

uint64_t resource_limits_manager::get_virtual_block_cpu_limit() const {
   //TODO:
   auto& db = _dbm.main_db();
   const auto& state = db.get<resource_limits_state_object>();
   return state.virtual_cpu_limit;
}

uint64_t resource_limits_manager::get_virtual_block_net_limit() const {
   //TODO:
   auto& db = _dbm.main_db();
   const auto& state = db.get<resource_limits_state_object>();
   return state.virtual_net_limit;
}


uint64_t resource_limits_manager::get_block_cpu_limit(const chainbase::database& shared_db, const chainbase::database& db) const {
   const auto* state = db.find<resource_limits_state_object>();
   const auto& config = shared_db.get<resource_limits_config_object>();
   if( state ){
      return config.cpu_limit_parameters.max - state->pending_cpu_usage;
   }
   return config.cpu_limit_parameters.max;
}


uint64_t resource_limits_manager::get_block_net_limit(const chainbase::database& shared_db ) const {
   const auto& config = shared_db.get<resource_limits_config_object>();
   return config.net_limit_parameters.max - get_block_pending_net();
}

std::pair<int64_t, bool> resource_limits_manager::get_account_cpu_limit( const account_name& name, chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit ) const {
   auto [arl, greylisted] = get_account_cpu_limit_ex(name, db, shared_db, greylist_limit);
   return {arl.available, greylisted};
}

std::pair<account_resource_limit, bool>
resource_limits_manager::get_account_cpu_limit_ex( const account_name& name, const chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit, const std::optional<block_timestamp_type>& current_time) const {

   const auto& state = shared_db.get<resource_limits_state_object>();
   const auto* usage  = db.find<resource_usage_object,by_owner>( name );
   usage_accumulator cpu_usage;
   if (usage) {
      cpu_usage = usage->net_usage;
   }

   const auto& config = shared_db.get<resource_limits_config_object>();

   const auto* shard_state = db.find<resource_limits_state_object>();
   EOS_ASSERT(shard_state, eosio::chain::shard_exception, "resource_limits_state_object not found on shard");
   int64_t cpu_weight, x, y;
   get_account_limits( name, x, y, cpu_weight, shared_db );

   if( cpu_weight < 0 || state.total_cpu_weight == 0 ) {
      return {{ -1, -1, -1, block_timestamp_type(cpu_usage.last_ordinal), -1 }, false};
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_cpu_usage_average_window;

   bool greylisted = false;
   uint128_t virtual_cpu_capacity_in_window = window_size;
   if( greylist_limit < config::maximum_elastic_resource_multiplier ) {
      uint64_t greylisted_virtual_cpu_limit = config.cpu_limit_parameters.max * greylist_limit;
      if( greylisted_virtual_cpu_limit < shard_state->virtual_cpu_limit ) {
         virtual_cpu_capacity_in_window *= greylisted_virtual_cpu_limit;
         greylisted = true;
      } else {
         virtual_cpu_capacity_in_window *= shard_state->virtual_cpu_limit;
      }
   } else {
      virtual_cpu_capacity_in_window *= shard_state->virtual_cpu_limit;
   }

   uint128_t user_weight     = (uint128_t)cpu_weight;
   uint128_t all_user_weight = (uint128_t)state.total_cpu_weight;

   auto max_user_use_in_window = (virtual_cpu_capacity_in_window * user_weight) / all_user_weight;
   auto cpu_used_in_window  = impl::integer_divide_ceil((uint128_t)cpu_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= cpu_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - cpu_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(cpu_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   arl.last_usage_update_time = block_timestamp_type(cpu_usage.last_ordinal);
   arl.current_used = arl.used;
   if ( current_time ) {
      if (current_time->slot > cpu_usage.last_ordinal) {
         // auto history_usage = usage->cpu_usage;
         cpu_usage.add(0, current_time->slot, window_size);
         arl.current_used = impl::downgrade_cast<int64_t>(impl::integer_divide_ceil((uint128_t)cpu_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision));
      }
   }
   return {arl, greylisted};
}

std::pair<int64_t, bool> resource_limits_manager::get_account_net_limit( const account_name& name, chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit ) const {
   auto [arl, greylisted] = get_account_net_limit_ex(name, db, shared_db, greylist_limit);
   return {arl.available, greylisted};
}

std::pair<account_resource_limit, bool>
resource_limits_manager::get_account_net_limit_ex( const account_name& name, const chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit, const std::optional<block_timestamp_type>& current_time) const {
   const auto& config = shared_db.get<resource_limits_config_object>();
   const auto& state  = shared_db.get<resource_limits_state_object>();
   const auto* usage  = db.find<resource_usage_object, by_owner>( name );
   usage_accumulator net_usage;
   if (usage) {
      net_usage = usage->net_usage;
   }

   int64_t net_weight, x, y;
   get_account_limits( name, x, net_weight, y, shared_db );

   if( net_weight < 0 || state.total_net_weight == 0) {
      return {{ -1, -1, -1, block_timestamp_type(net_usage.last_ordinal), -1 }, false};
   }

   account_resource_limit arl;

   uint128_t window_size = config.account_net_usage_average_window;

   bool greylisted = false;
   uint128_t virtual_network_capacity_in_window = window_size;
   if( greylist_limit < config::maximum_elastic_resource_multiplier ) {
      uint64_t greylisted_virtual_net_limit = config.net_limit_parameters.max * greylist_limit;
      if( greylisted_virtual_net_limit < state.virtual_net_limit ) {
         virtual_network_capacity_in_window *= greylisted_virtual_net_limit;
         greylisted = true;
      } else {
         virtual_network_capacity_in_window *= state.virtual_net_limit;
      }
   } else {
      virtual_network_capacity_in_window *= state.virtual_net_limit;
   }

   uint128_t user_weight     = (uint128_t)net_weight;
   uint128_t all_user_weight = (uint128_t)state.total_net_weight;

   auto max_user_use_in_window = (virtual_network_capacity_in_window * user_weight) / all_user_weight;
   auto net_used_in_window  = impl::integer_divide_ceil((uint128_t)net_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision);

   if( max_user_use_in_window <= net_used_in_window )
      arl.available = 0;
   else
      arl.available = impl::downgrade_cast<int64_t>(max_user_use_in_window - net_used_in_window);

   arl.used = impl::downgrade_cast<int64_t>(net_used_in_window);
   arl.max = impl::downgrade_cast<int64_t>(max_user_use_in_window);
   arl.last_usage_update_time = block_timestamp_type(net_usage.last_ordinal);
   arl.current_used = arl.used;
   if ( current_time ) {
      if (current_time->slot > net_usage.last_ordinal) {
         net_usage.add(0, current_time->slot, window_size);
         arl.current_used = impl::downgrade_cast<int64_t>(impl::integer_divide_ceil((uint128_t)net_usage.value_ex * window_size, (uint128_t)config::rate_limiting_precision));
      }
   }
   return {arl, greylisted};
}

const resource_usage_object& resource_limits_manager::get_or_create_usage_object(const account_name& name, chainbase::database& db) {
   const auto* usage  = db.find<resource_usage_object,by_owner>( name );
   if( usage != nullptr ) {
      return *usage;
   }

   return db.create<resource_usage_object>([&]( resource_usage_object& bu ) {
         bu.owner = name;
   });
}

} } } /// eosio::chain::resource_limits
