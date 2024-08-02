#pragma once
#include <eosio/chain/controller.hpp>
#include <eosio/chain/transaction.hpp>
#include <eosio/chain/transaction_context.hpp>
// #include <eosio/chain/contract_table_objects.hpp>
#include <eosio/chain/deep_mind.hpp>
#include <fc/utility.hpp>
#include <sstream>
#include <algorithm>
#include <set>

namespace chainbase { class database; }

namespace eosio { namespace chain {

class controller;
// struct contract_table_context;

template<typename T>
class contract_table_context_base;
struct contract_tables;
struct contract_shared_tables;

typedef contract_table_context_base<eosio::chain::contract_tables> contract_table_context;
typedef contract_table_context_base<eosio::chain::contract_shared_tables> contract_shared_table_context;

class apply_context {

   /// Constructor
   public:
      apply_context(controller& con, transaction_context& trx_ctx, uint32_t action_ordinal, uint32_t depth=0);
      ~apply_context();

   /// Execution methods:
   public:

      void exec_one();
      void exec();
      void execute_inline( action&& a );
      void execute_context_free_inline( action&& a );
      void schedule_deferred_transaction( const uint128_t& sender_id, account_name payer, transaction&& trx, bool replace_existing );
      bool cancel_deferred_transaction( const uint128_t& sender_id, account_name sender );
      bool cancel_deferred_transaction( const uint128_t& sender_id ) { return cancel_deferred_transaction(sender_id, receiver); }

   protected:
      uint32_t schedule_action( uint32_t ordinal_of_action_to_schedule, account_name receiver, bool context_free );
      uint32_t schedule_action( action&& act_to_schedule, account_name receiver, bool context_free );


   /// Authorization methods:
   public:

      /**
       * @brief Require @ref account to have approved of this message
       * @param account The account whose approval is required
       *
       * This method will check that @ref account is listed in the message's declared authorizations, and marks the
       * authorization as used. Note that all authorizations on a message must be used, or the message is invalid.
       *
       * @throws missing_auth_exception If no sufficient permission was found
       */
      void require_authorization(const account_name& account);
      bool has_authorization(const account_name& account) const;
      void require_authorization(const account_name& account, const permission_name& permission);

      /**
       * @return true if account exists, false if it does not
       */
      bool is_account(const account_name& account)const;

      void get_code_hash(
         account_name account, uint64_t& code_sequence, fc::sha256& code_hash, uint8_t& vm_type, uint8_t& vm_version) const;

      /**
       * Requires that the current action be delivered to account
       */
      void require_recipient(account_name account);

      /**
       * Return true if the current action has already been scheduled to be
       * delivered to the specified account.
       */
      bool has_recipient(account_name account)const;

   /// Console methods:
   public:

      void console_append( std::string_view val ) {
         _pending_console_output += val;
      }

   /// Database methods:
   public:

      void update_db_usage( const account_name& payer, int64_t delta );

   /// Misc methods:
   public:


      int get_action( uint32_t type, uint32_t index, char* buffer, size_t buffer_size )const;
      int get_context_free_data( uint32_t index, char* buffer, size_t buffer_size )const;
      vector<account_name> get_active_producers() const;

      uint64_t next_global_sequence();
      uint64_t next_recv_sequence( const account_metadata_object& receiver_account );
      uint64_t next_auth_sequence( account_name actor );

      void add_ram_usage( account_name account, int64_t ram_delta );
      void finalize_trace( action_trace& trace, const fc::time_point& start );

      bool is_context_free()const { return context_free; }
      bool is_privileged()const { return privileged; }
      bool is_main_shard()const { return shard_name == config::main_shard_name; }
      const account_name& get_receiver()const { return receiver; }
      const action& get_action()const { return *act; }

      action_name get_sender() const;

      contract_table_context& table_context();
      contract_shared_table_context& shared_table_context();

      bool is_builtin_activated( builtin_protocol_feature_t f ) const;
      bool is_speculative_block() const;

   protected:
      const account_metadata_object& get_account_metadata(const name& account);
   /// Fields:
   public:
      controller&                   control;
      chainbase::database&          db;  ///< database where state is stored
      transaction_context&          trx_context; ///< transaction context in which the action is running
      eosio::chain::shard_name      shard_name = config::main_shard_name;
      chainbase::database&          shared_db;
   private:
      const action*                 act = nullptr; ///< action being applied
      // act pointer may be invalidated on call to trx_context.schedule_action
      account_name                  receiver; ///< the code that is currently running
      uint32_t                      recurse_depth; ///< how deep inline actions can recurse
      uint32_t                      first_receiver_action_ordinal = 0;
      uint32_t                      action_ordinal = 0;
      bool                          privileged   = false;
      bool                          context_free = false;

   public:
      std::vector<char>             action_return_value;

   private:
      std::unique_ptr<contract_table_context>         _contract_table;
      std::unique_ptr<contract_shared_table_context>  _contract_shared_table;
      vector< std::pair<account_name, uint32_t> >     _notified; ///< keeps track of new accounts to be notifed of current message
      vector<uint32_t>                    _inline_actions; ///< action_ordinals of queued inline actions
      vector<uint32_t>                    _cfa_inline_actions; ///< action_ordinals of queued inline context-free actions
      std::string                         _pending_console_output;
      flat_set<account_delta>             _account_ram_deltas; ///< flat_set of account_delta so json is an array of objects

      //bytes                               _cached_trx;
};

using apply_handler = std::function<void(apply_context&)>;

} } // namespace eosio::chain

//FC_REFLECT(eosio::chain::apply_context::apply_results, (applied_actions)(deferred_transaction_requests)(deferred_transactions_count))
