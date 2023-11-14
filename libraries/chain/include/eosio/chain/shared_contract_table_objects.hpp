#pragma once

#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/multi_index_includes.hpp>
#include <eosio/chain/contract_table_objects.hpp>

#include <array>
#include <type_traits>

#define declared_in_contract_table_objects_hpp 1

namespace eosio { namespace chain {

   /**
    * @brief The shared_table_id_object class tracks the mapping of (scope, code, table) to an opaque identifier
    */
   class shared_table_id_object : public chainbase::object<table_id_object_type, shared_table_id_object> {
      OBJECT_CTOR(shared_table_id_object)

      id_type        id;
      account_name   code;  //< code should not be changed within a chainbase modifier lambda
      scope_name     scope; //< scope should not be changed within a chainbase modifier lambda
      table_name     table; //< table should not be changed within a chainbase modifier lambda
      account_name   payer;
      uint32_t       count = 0; /// the number of elements in the table
   };

   struct by_code_scope_table;

   using shared_table_id_multi_index = chainbase::shared_multi_index_container<
      shared_table_id_object,
      indexed_by<
         ordered_unique<tag<by_id>,
            member<shared_table_id_object, shared_table_id_object::id_type, &shared_table_id_object::id>
         >,
         ordered_unique<tag<by_code_scope_table>,
            composite_key< shared_table_id_object,
               member<shared_table_id_object, account_name, &shared_table_id_object::code>,
               member<shared_table_id_object, scope_name,   &shared_table_id_object::scope>,
               member<shared_table_id_object, table_name,   &shared_table_id_object::table>
            >
         >
      >
   >;

   using shared_table_id = shared_table_id_object::id_type;

   struct by_scope_primary;
   struct by_scope_secondary;
   struct by_scope_tertiary;


   struct shared_key_value_object : public chainbase::object<key_value_object_type, shared_key_value_object> {
      OBJECT_CTOR(shared_key_value_object, (value))

      typedef uint64_t key_type;
      static const int number_of_keys = 1;

      id_type               id;
      shared_table_id              t_id; //< t_id should not be changed within a chainbase modifier lambda
      uint64_t              primary_key; //< primary_key should not be changed within a chainbase modifier lambda
      account_name          payer;
      shared_blob           value;
   };

   using shared_key_value_index = chainbase::shared_multi_index_container<
      shared_key_value_object,
      indexed_by<
         ordered_unique<tag<by_id>, member<shared_key_value_object, shared_key_value_object::id_type, &shared_key_value_object::id>>,
         ordered_unique<tag<by_scope_primary>,
            composite_key< shared_key_value_object,
               member<shared_key_value_object, shared_table_id, &shared_key_value_object::t_id>,
               member<shared_key_value_object, uint64_t, &shared_key_value_object::primary_key>
            >,
            composite_key_compare< std::less<shared_table_id>, std::less<uint64_t> >
         >
      >
   >;

   struct by_primary;
   struct by_secondary;

   template<typename SecondaryKey, uint64_t ObjectTypeId, typename SecondaryKeyLess = std::less<SecondaryKey> >
   struct shared_secondary_index
   {
      struct index_object : public chainbase::object<ObjectTypeId,index_object> {
         OBJECT_CTOR(index_object)
         typedef SecondaryKey secondary_key_type;

         typename chainbase::object<ObjectTypeId,index_object>::id_type       id;
         shared_table_id      t_id; //< t_id should not be changed within a chainbase modifier lambda
         uint64_t      primary_key; //< primary_key should not be changed within a chainbase modifier lambda
         account_name  payer;
         SecondaryKey  secondary_key; //< secondary_key should not be changed within a chainbase modifier lambda
      };


      typedef chainbase::shared_multi_index_container<
         index_object,
         indexed_by<
            ordered_unique<tag<by_id>, member<index_object, typename index_object::id_type, &index_object::id>>,
            ordered_unique<tag<by_primary>,
               composite_key< index_object,
                  member<index_object, shared_table_id, &index_object::t_id>,
                  member<index_object, uint64_t, &index_object::primary_key>
               >,
               composite_key_compare< std::less<shared_table_id>, std::less<uint64_t> >
            >,
            ordered_unique<tag<by_secondary>,
               composite_key< index_object,
                  member<index_object, shared_table_id, &index_object::t_id>,
                  member<index_object, SecondaryKey, &index_object::secondary_key>,
                  member<index_object, uint64_t, &index_object::primary_key>
               >,
               composite_key_compare< std::less<shared_table_id>, SecondaryKeyLess, std::less<uint64_t> >
            >
         >
      > index_index;
   };

   typedef shared_secondary_index<uint64_t,index64_object_type>::index_object   shared_index64_object;
   typedef shared_secondary_index<uint64_t,index64_object_type>::index_index    shared_index64_index;

   typedef shared_secondary_index<uint128_t,index128_object_type>::index_object shared_index128_object;
   typedef shared_secondary_index<uint128_t,index128_object_type>::index_index  shared_index128_index;

#ifndef declared_in_contract_table_objects_hpp
   typedef std::array<uint128_t, 2> key256_t;
#endif
   typedef shared_secondary_index<key256_t,index256_object_type>::index_object shared_index256_object;
   typedef shared_secondary_index<key256_t,index256_object_type>::index_index  shared_index256_index;

#ifndef declared_in_contract_table_objects_hpp
   struct soft_double_less {
      bool operator()( const float64_t& lhs, const float64_t& rhs ) const {
         return f64_lt( lhs, rhs );
      }
   };

   struct soft_long_double_less {
      bool operator()( const float128_t& lhs, const float128_t& rhs ) const {
         return f128_lt( lhs, rhs );
      }
   };
#endif//declared_in_contract_table_objects_hpp

   /**
    *  This index supports a deterministic software implementation of double as the secondary key.
    *
    *  The software double implementation is using the Berkeley softfloat library (release 3).
    */

   typedef shared_secondary_index<float64_t,index_double_object_type,soft_double_less>::index_object  shared_index_double_object;
   typedef shared_secondary_index<float64_t,index_double_object_type,soft_double_less>::index_index   shared_index_double_index;

   /**
    *  This index supports a deterministic software implementation of long double as the secondary key.
    *
    *  The software long double implementation is using the Berkeley softfloat library (release 3).
    */
   typedef shared_secondary_index<float128_t,index_long_double_object_type,soft_long_double_less>::index_object  shared_index_long_double_object;
   typedef shared_secondary_index<float128_t,index_long_double_object_type,soft_long_double_less>::index_index   shared_index_long_double_index;

#ifndef declared_in_contract_table_objects_hpp
   template<typename T>
   struct secondary_key_traits {
      using value_type = std::enable_if_t<std::is_integral<T>::value, T>;

      static_assert( std::numeric_limits<value_type>::is_specialized, "value_type does not have specialized numeric_limits" );

      static constexpr value_type true_lowest() { return std::numeric_limits<value_type>::lowest(); }
      static constexpr value_type true_highest() { return std::numeric_limits<value_type>::max(); }
   };

   template<size_t N>
   struct secondary_key_traits<std::array<uint128_t, N>> {
   private:
      static constexpr uint128_t max_uint128 = (static_cast<uint128_t>(std::numeric_limits<uint64_t>::max()) << 64) | std::numeric_limits<uint64_t>::max();
      static_assert( std::numeric_limits<uint128_t>::max() == max_uint128, "numeric_limits for uint128_t is not properly defined" );

   public:
      using value_type = std::array<uint128_t, N>;

      static value_type true_lowest() {
         value_type arr;
         return arr;
      }

      static value_type true_highest() {
         value_type arr;
         for( auto& v : arr ) {
            v = std::numeric_limits<uint128_t>::max();
         }
         return arr;
      }
   };

   template<>
   struct secondary_key_traits<float64_t> {
      using value_type = float64_t;

      static value_type true_lowest() {
         return f64_negative_infinity();
      }

      static value_type true_highest() {
         return f64_positive_infinity();
      }
   };

   template<>
   struct secondary_key_traits<float128_t> {
      using value_type = float128_t;

      static value_type true_lowest() {
         return f128_negative_infinity();
      }

      static value_type true_highest() {
         return f128_positive_infinity();
      }
   };

   /**
    * helper template to map from an index type to the best tag
    * to use when traversing by shared_table_id
    */
   template<typename T>
   struct object_to_table_id_tag;

#define DECLARE_TABLE_ID_TAG( object, tag ) \
   template<> \
   struct object_to_table_id_tag<object> { \
      using tag_type = tag;\
   };
#endif

   DECLARE_TABLE_ID_TAG(shared_key_value_object, by_scope_primary)
   DECLARE_TABLE_ID_TAG(shared_index64_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index128_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index256_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index_double_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index_long_double_object, by_primary)

   template<typename T>
   using object_to_table_id_tag_t = typename object_to_table_id_tag<T>::tag_type;

namespace config {
   template<>
   struct billable_size<shared_table_id_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2;  ///< overhead for 2x indices internal-key and code,scope,table
      static const uint64_t value = 44 + overhead; ///< 44 bytes for constant size fields + overhead
   };

   template<>
   struct billable_size<shared_key_value_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2;  ///< overhead for potentially single-row table, 2x indices internal-key and primary key
      static const uint64_t value = 32 + 8 + 4 + overhead; ///< 32 bytes for our constant size fields, 8 for pointer to vector data, 4 bytes for a size of vector + overhead
   };

   template<>
   struct billable_size<shared_index64_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 8 + overhead; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index128_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 16 + overhead; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index256_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 32 + overhead; ///< 24 bytes for fixed fields + 32 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 8 + overhead; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index_long_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 16 + overhead; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

} // namespace config

} }  // namespace eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_table_id_object, eosio::chain::shared_table_id_multi_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_key_value_object, eosio::chain::shared_key_value_index)

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index64_object, eosio::chain::shared_index64_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index128_object, eosio::chain::shared_index128_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index256_object, eosio::chain::shared_index256_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index_double_object, eosio::chain::shared_index_double_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index_long_double_object, eosio::chain::shared_index_long_double_index)

FC_REFLECT(eosio::chain::shared_table_id_object, (code)(scope)(table)(payer)(count) )
FC_REFLECT(eosio::chain::shared_key_value_object, (primary_key)(payer)(value) )

#define REFLECT_SECONDARY(type)\
  FC_REFLECT(type, (primary_key)(payer)(secondary_key) )

REFLECT_SECONDARY(eosio::chain::shared_index64_object)
REFLECT_SECONDARY(eosio::chain::shared_index128_object)
REFLECT_SECONDARY(eosio::chain::shared_index256_object)
REFLECT_SECONDARY(eosio::chain::shared_index_double_object)
REFLECT_SECONDARY(eosio::chain::shared_index_long_double_object)
