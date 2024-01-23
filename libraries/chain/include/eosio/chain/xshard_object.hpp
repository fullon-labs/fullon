#pragma once
#include <fc/io/raw.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {
   using boost::multi_index_container;
   using namespace boost::multi_index;
   /**
    * The purpose of this object is to hold tokens that are transfered between shards.
    */
   class xshard_object : public chainbase::object<xshard_object_type, xshard_object>
   {
      OBJECT_CTOR(xshard_object, (action_data))

      id_type                       id;
      xshard_id_type                xsh_id;
      account_name                  owner;
      shard_name                    from_shard;
      shard_name                    to_shard;
      account_name                  contract;
      action_name                   action_type;
      shared_blob                   action_data;
      transaction_id_type           scheduled_xshin_trx;

      xshard_object& operator=(const xshard_object& a) {
         id                = a.id;
         xsh_id            = a.xsh_id;
         owner             = a.owner;
         from_shard        = a.from_shard;
         to_shard          = a.to_shard;
         contract          = a.contract;
         action_type       = a.action_type;
         action_data.assign(a.action_data.data(), a.action_data.size());
         scheduled_xshin_trx = a.scheduled_xshin_trx;
         return *this;
      }

      static inline xshard_id_type make_xsh_id(const transaction_id_type& trx_id, uint32_t trx_action_sequence) {
         digest_type::encoder enc;
         fc::raw::pack( enc, trx_id );
         fc::raw::pack( enc, trx_action_sequence  );
         return enc.result();
      }
   };

   struct by_xshard_id;
   struct by_owner_xshard;
   using xshard_index = chainbase::shared_multi_index_container<
      xshard_object,
      indexed_by<
         ordered_unique< tag<by_id>, BOOST_MULTI_INDEX_MEMBER(xshard_object, xshard_object::id_type, id)>,
         ordered_unique< tag<by_xshard_id>, BOOST_MULTI_INDEX_MEMBER(xshard_object, digest_type, xsh_id) >,
         ordered_unique< tag<by_owner_xshard>,
            composite_key< xshard_object,
               BOOST_MULTI_INDEX_MEMBER( xshard_object, account_name, owner ),
               BOOST_MULTI_INDEX_MEMBER( xshard_object, xshard_object::id_type, id)
            >
         >
      >
   >;
} /* namespace chain */ } /* namespace eosio */

CHAINBASE_SET_INDEX_TYPE(eosio::chain::xshard_object, eosio::chain::xshard_index)

FC_REFLECT(eosio::chain::xshard_object, (xsh_id)(owner)(from_shard)(to_shard)(contract)(action_type)(action_data)(scheduled_xshin_trx))


