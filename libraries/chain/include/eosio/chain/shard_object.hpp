#pragma once
#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/authority.hpp>
#include <eosio/chain/code_object.hpp>
#include <eosio/chain/block_timestamp.hpp>
#include <eosio/chain/abi_def.hpp>

#include "multi_index_includes.hpp"

namespace eosio { namespace chain {

   class shard_object : public chainbase::object<shard_object_type, shard_object> {
      OBJECT_CTOR(shard_object)

      id_type              id;
      shard_name           name;                      //< name should not be changed within a chainbase modifier lambda
      bool                 enabled        = false;
      block_timestamp_type creation_at;

      shard_object& operator=(const shard_object& a) {
         id             = a.id;
         name           = a.name;
         enabled        = a.enabled;
         creation_at    = a.creation_at;
         return *this;
      }
   };
   using shard_id_type = shard_object::id_type;

   struct by_name;
   using shard_index = chainbase::shared_multi_index_container<
      shard_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<shard_object, shard_object::id_type, &shard_object::id>>,
         ordered_unique<tag<by_name>, member<shard_object, shard_name, &shard_object::name>>
      >
   >;

   class shard_change_object : public chainbase::object<shard_change_object_type, shard_change_object>
   {
      OBJECT_CTOR(shard_change_object);

      enum class change_type : uint32_t {
         create = 0,
         modify = 1
      };

      id_type               id;
      shard_name            name; //< name should not be changed within a chainbase modifier lambda
      bool                  enabled        = false;
      block_num_type        block_num;
   };

   struct by_name;
   struct by_block_num;
   using shard_change_index = chainbase::shared_multi_index_container<
      shard_change_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<shard_change_object, shard_change_object::id_type, &shard_change_object::id>>,
         ordered_unique<tag<by_name>, member<shard_change_object, shard_name, &shard_change_object::name>>
      >
   >;

} } // eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shard_object, eosio::chain::shard_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shard_change_object, eosio::chain::shard_change_index)

FC_REFLECT(eosio::chain::shard_object, (name)(enabled)(creation_at))
FC_REFLECT(eosio::chain::shard_change_object, (name)(enabled)(block_num))
