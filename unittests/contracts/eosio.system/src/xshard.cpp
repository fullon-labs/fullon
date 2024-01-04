#include <eosio.system/eosio.system.hpp>
#include <eosio.token/eosio.token.hpp>


namespace eosiosystem {



void native::xshout( name                 owner,
                     name                 to_shard,
                     name                 contract,
                     name                 action_type,
                     std::vector<char>    action_data )
{
   require_auth(owner);
   // check(is_shard(to_shard), "to_shard is not a valid shard");
   if (action_type == "xtoken"_n) {
      std::vector<permission_level> perms = {
            { owner, system_contract::active_permission },
            { _self, system_contract::active_permission }
      };
      eosio::token::xshout_action xshout_act{ contract, std::move(perms) };

      auto x = eosio::unpack<xtoken>(action_data);
      xshout_act.send( owner, to_shard, x.quantity, x.memo );
   } else {
      check(false, "unsupported action type");
   }
}

void native::xshin( const name& owner, const checksum256& xsh_id ) {
   // require_auth(owner); // TODO: need this require auth?

   eosio::xshard xsh;
   get_xshard( xsh_id, xsh );

   check(owner == xsh.owner, "owner mismatch");

   if (xsh.action_type == "xtoken"_n) {
      std::vector<permission_level> perms = {
            { owner, system_contract::active_permission },
            { _self, system_contract::active_permission }
      };
      eosio::token::xshin_action xshin_act{ xsh.contract, std::move(perms) };
      auto x = eosio::unpack<xtoken>(xsh.action_data);
      xshin_act.send( owner, xsh.from_shard, x.quantity, x.memo );
   } else {
      check(false, "unsupported action type");
   }
}

}; /// namespace eosiosystem
