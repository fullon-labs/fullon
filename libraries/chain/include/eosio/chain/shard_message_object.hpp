#pragma once
#include <fc/io/raw.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;
   /**
    * The purpose of this object is to hold messages that are communicated between shards.
    */
   class shard_message_object : public chainbase::object<shard_message_object_type, shard_message_object>
   {
         OBJECT_CTOR(shard_message_object)

         id_type                       id;
         account_name                  owner;
         shard_name                    from_shard;
         shard_name                    to_shard;
         account_name                  contract;
         eosio::chain::action_name     action_name;
         bytes                         action_data;
         transaction_id_type           trx_id;
         uint32_t                      trx_action_sequence;


         message_id_type msg_id() const {
            digest_type::encoder enc;
            fc::raw::pack( enc, trx_id );
            fc::raw::pack( enc, trx_action_sequence  );
            return enc.result();
      ; }
   };

   struct by_msg_id;
   struct by_owner_msg;
   using shard_message_index = chainbase::shared_multi_index_container<
      shard_message_object,
      indexed_by<
         ordered_unique< tag<by_id>, BOOST_MULTI_INDEX_MEMBER(shard_message_object, shard_message_object::id_type, id)>,
         ordered_unique< tag<by_msg_id>, BOOST_MULTI_INDEX_CONST_MEM_FUN(shard_message_object, digest_type, msg_id) >,
         ordered_unique< tag<by_owner_msg>,
            composite_key< shard_message_object,
               BOOST_MULTI_INDEX_MEMBER( shard_message_object, account_name, owner ),
               BOOST_MULTI_INDEX_MEMBER( shard_message_object, shard_message_object::id_type, id)
            >
         >
      >
   >;
} /* namespace chain */ } /* namespace eosio */

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shard_message_object, eosio::chain::shard_message_index)

FC_REFLECT(eosio::chain::shard_message_object, (owner)(from_shard)(to_shard)(contract)(action_name)(action_data)(trx_id)(trx_action_sequence))


