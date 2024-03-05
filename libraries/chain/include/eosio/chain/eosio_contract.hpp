#pragma once

#include <eosio/chain/types.hpp>
#include <eosio/chain/contract_types.hpp>

namespace eosio { namespace chain {

   class apply_context;

   /**
    * @defgroup native_action_handlers Native Action Handlers
    */
   ///@{
   void apply_flon_newaccount(apply_context&);
   void apply_flon_updateauth(apply_context&);
   void apply_flon_deleteauth(apply_context&);
   void apply_flon_linkauth(apply_context&);
   void apply_flon_unlinkauth(apply_context&);

   /*
   void apply_eosio_postrecovery(apply_context&);
   void apply_eosio_passrecovery(apply_context&);
   void apply_eosio_vetorecovery(apply_context&);
   */

   void apply_flon_setcode(apply_context&);
   void apply_flon_setabi(apply_context&);

   void apply_flon_canceldelay(apply_context&);

   void apply_flon_xshout(apply_context&);
   void apply_flon_xshin(apply_context&);
   ///@}  end action handlers

} } /// namespace eosio::chain
