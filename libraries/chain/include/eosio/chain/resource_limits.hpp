#pragma once
#include <eosio/chain/exceptions.hpp>
#include <eosio/chain/types.hpp>
#include <eosio/chain/config.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <chainbase/chainbase.hpp>
#include <eosio/chain/database_manager.hpp>
#include <set>
#include <shared_mutex>
#include <mutex>

namespace eosio { namespace chain {

   class deep_mind_handler;

   namespace resource_limits {
   struct resource_usage_object;
   namespace impl {
      template<typename T>
      struct ratio {
         static_assert(std::is_integral<T>::value, "ratios must have integral types");
         T numerator;
         T denominator;

         friend inline bool operator ==( const ratio& lhs, const ratio& rhs ) {
            return std::tie(lhs.numerator, lhs.denominator) == std::tie(rhs.numerator, rhs.denominator);
         }

         friend inline bool operator !=( const ratio& lhs, const ratio& rhs ) {
            return !(lhs == rhs);
         }
      };
   }

   using ratio = impl::ratio<uint64_t>;

   struct elastic_limit_parameters {
      uint64_t target;           // the desired usage
      uint64_t max;              // the maximum usage
      uint32_t periods;          // the number of aggregation periods that contribute to the average usage

      uint32_t max_multiplier;   // the multiplier by which virtual space can oversell usage when uncongested
      ratio    contract_rate;    // the rate at which a congested resource contracts its limit
      ratio    expand_rate;       // the rate at which an uncongested resource expands its limits

      void validate()const; // throws if the parameters do not satisfy basic sanity checks

      friend inline bool operator ==( const elastic_limit_parameters& lhs, const elastic_limit_parameters& rhs ) {
         return std::tie(lhs.target, lhs.max, lhs.periods, lhs.max_multiplier, lhs.contract_rate, lhs.expand_rate)
                  == std::tie(rhs.target, rhs.max, rhs.periods, rhs.max_multiplier, rhs.contract_rate, rhs.expand_rate);
      }

      friend inline bool operator !=( const elastic_limit_parameters& lhs, const elastic_limit_parameters& rhs ) {
         return !(lhs == rhs);
      }
   };

   struct account_resource_limit {
      int64_t used = 0; ///< quantity used in current window
      int64_t available = 0; ///< quantity available in current window (based upon fractional reserve)
      int64_t max = 0; ///< max per window under current congestion
      block_timestamp_type last_usage_update_time; ///< last usage timestamp
      int64_t current_used = 0;  ///< current usage according to the given timestamp
   };
   struct block_pending_net {
      mutable std::shared_mutex           rw_lock;
      uint64_t                            pending_net_usage = 0ULL;
   };

   class resource_limits_manager {
      public:

         explicit resource_limits_manager( eosio::chain::database_manager& dbm, std::function<deep_mind_handler*(bool is_trx_transient)> get_deep_mind_logger )
         :_dbm(dbm)
         ,_get_deep_mind_logger(get_deep_mind_logger)
         {
            _block_pending_net_usage = std::make_shared<block_pending_net>();
         }

         static void add_indices(chainbase::database& db);
         static void add_shared_indices(chainbase::database& shared_db);
         static void copy_data(chainbase::database& main_db, chainbase::database& shared_db);
         static void copy_changes(chainbase::database& main_db, chainbase::database& shared_db);
         void initialize_database();
         static void add_to_snapshot( chainbase::database& db, const snapshot_shard_writer_ptr& snapshot );
         static void read_from_snapshot( chainbase::database& db, const snapshot_shard_reader_ptr& snapshot );

         void initialize_account( const account_name& account, bool is_trx_transient );
         void set_block_parameters( const elastic_limit_parameters& cpu_limit_parameters, const elastic_limit_parameters& net_limit_parameters );

         void update_account_usage( const flat_set<account_name>& accounts, uint32_t ordinal, chainbase::database& db, chainbase::database& shared_db);
         void add_transaction_usage( const flat_set<account_name>& accounts, uint64_t cpu_usage, uint64_t net_usage, uint32_t ordinal, chainbase::database& db, chainbase::database& shared_db, bool is_trx_transient = false );

         void add_pending_ram_usage( const account_name account, int64_t ram_delta, chainbase::database& db, bool is_trx_transient = false );
         void verify_account_ram_usage( const account_name accunt, chainbase::database& db, chainbase::database& shared_db )const;

         /// set_account_limits returns true if new ram_bytes limit is more restrictive than the previously set one
         bool set_account_limits( const account_name& account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight, chainbase::database& shared_db, bool is_trx_transient);
         void get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight, const chainbase::database& shared_db) const;
         void get_account_limits( const account_name& account, int64_t& ram_bytes, int64_t& net_weight, int64_t& cpu_weight) const;

         bool is_unlimited_cpu( const account_name& account, const chainbase::database& shared_db) const;

         void process_account_limit_updates();
         void process_block_usage( uint32_t block_num, std::vector<chainbase::database*> processing_shard = {} );

         // accessors
         uint64_t get_total_cpu_weight() const;
         uint64_t get_total_net_weight() const;

         uint64_t get_virtual_block_cpu_limit() const;
         uint64_t get_virtual_block_net_limit() const;

         uint64_t get_block_cpu_limit( const chainbase::database& shared_db, const chainbase::database& db ) const;
         uint64_t get_block_net_limit( const chainbase::database& shared_db ) const;

         std::pair<int64_t, bool> get_account_cpu_limit( const account_name& name, chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier ) const;
         std::pair<int64_t, bool> get_account_net_limit( const account_name& name, chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier ) const;

         std::pair<account_resource_limit, bool>
         get_account_cpu_limit_ex( const account_name& name, const chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier, const std::optional<block_timestamp_type>& current_time={} ) const;

         std::pair<account_resource_limit, bool>
         get_account_net_limit_ex( const account_name& name, const chainbase::database& db, const chainbase::database& shared_db, uint32_t greylist_limit = config::maximum_elastic_resource_multiplier, const std::optional<block_timestamp_type>& current_time={} ) const;

         int64_t get_account_ram_usage( const account_name& name , chainbase::database& db) const;
         int64_t get_account_ram_usage( const account_name& name ) const;
         std::shared_mutex& get_net_lock(){ return _block_pending_net_usage->rw_lock; }
         void init_block_pending_net(){
            std::unique_lock write_lock( get_net_lock() );
            _block_pending_net_usage->pending_net_usage = 0ULL;
         }
         uint64_t get_block_pending_net() const {
            std::shared_lock read_lock( _block_pending_net_usage->rw_lock );
            return _block_pending_net_usage->pending_net_usage;
         }
         void add_block_pending_net( uint64_t usage ){
            std::unique_lock write_lock( get_net_lock() );
            EOS_ASSERT( UINT64_MAX - _block_pending_net_usage->pending_net_usage >= usage, transaction_exception,
            "transaction Net usage adding would overflow UINT64_MAX");
            _block_pending_net_usage->pending_net_usage += usage;
         }
         void undo_block_pending_net( uint64_t usage ){
            std::unique_lock write_lock( get_net_lock() );
            EOS_ASSERT( _block_pending_net_usage->pending_net_usage >= usage, transaction_exception,
              "transaction Net usage is bigger than pending_net_usage");
            _block_pending_net_usage->pending_net_usage -= usage;
         }
         void ensure_resource_limits_state_object(chainbase::database& db, const chainbase::database& shared_db) const;
      private:
         eosio::chain::database_manager&     _dbm;
         std::function<deep_mind_handler*(bool is_trx_transient)> _get_deep_mind_logger;

         const resource_usage_object& get_or_create_usage_object(const account_name& name, chainbase::database& db);
      public:
         std::shared_ptr<block_pending_net> _block_pending_net_usage;
   };
} } } /// eosio::chain

FC_REFLECT( eosio::chain::resource_limits::account_resource_limit, (used)(available)(max)(last_usage_update_time)(current_used) )
FC_REFLECT( eosio::chain::resource_limits::ratio, (numerator)(denominator))
FC_REFLECT( eosio::chain::resource_limits::elastic_limit_parameters, (target)(max)(periods)(max_multiplier)(contract_rate)(expand_rate))
