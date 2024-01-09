#pragma once

#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/contract_types.hpp>
#include <eosio/chain/multi_index_includes.hpp>

#include <array>
#include <type_traits>

namespace eosio { namespace chain {


   struct by_code_scope_table;

   template<uint64_t ObjectTypeId>
   class table_id_index_container_base {
      public:
      /**
       * @brief The table_id_object class tracks the mapping of (scope, code, table) to an opaque identifier
       */
      class table_id_object : public chainbase::object<ObjectTypeId, table_id_object> {
         OBJECT_CTOR(table_id_object)

         using object_id_type = typename chainbase::object<ObjectTypeId,table_id_object>::id_type;

         object_id_type    id;
         account_name      code;  //< code should not be changed within a chainbase modifier lambda
         scope_name        scope; //< scope should not be changed within a chainbase modifier lambda
         table_name        table; //< table should not be changed within a chainbase modifier lambda
         account_name      payer;
         uint32_t          count = 0; /// the number of elements in the table
      };

      typedef typename table_id_object::id_type    object_id_type;

      typedef chainbase::shared_multi_index_container<
         table_id_object,
         indexed_by<
            ordered_unique<tag<by_id>,
               member<table_id_object, object_id_type, &table_id_object::id>
            >,
            ordered_unique<tag<by_code_scope_table>,
               composite_key< table_id_object,
                  member<table_id_object, account_name, &table_id_object::code>,
                  member<table_id_object, scope_name,   &table_id_object::scope>,
                  member<table_id_object, table_name,   &table_id_object::table>
               >
            >
         >
      > table_id_index;
   };

   typedef table_id_index_container_base<table_id_object_type>    table_id_index_container;
   typedef table_id_index_container::table_id_object              table_id_object;
   typedef table_id_index_container::table_id_index               table_id_multi_index;
   typedef table_id_index_container::object_id_type               table_id;

   typedef table_id_index_container_base<shared_table_id_object_type>    shared_table_id_index_container;
   typedef shared_table_id_index_container::table_id_object              shared_table_id_object;
   typedef shared_table_id_index_container::table_id_index               shared_table_id_multi_index;
   typedef shared_table_id_index_container::object_id_type                     shared_table_id;

   struct by_scope_primary;
   struct by_scope_secondary;
   struct by_scope_tertiary;



   template<typename TableIdType, uint64_t ObjectTypeId>
   struct key_value_index_container_base {
      struct key_value_object : public chainbase::object<ObjectTypeId, key_value_object> {
         OBJECT_CTOR(key_value_object, (value))

         typedef uint64_t key_type;
         static const int number_of_keys = 1;
         using object_id_type = typename chainbase::object<ObjectTypeId,key_value_object>::id_type;

         object_id_type        id;
         TableIdType           t_id; //< t_id should not be changed within a chainbase modifier lambda
         uint64_t              primary_key; //< primary_key should not be changed within a chainbase modifier lambda
         account_name          payer;
         shared_blob           value;
      };

      typedef typename key_value_object::id_type     object_id_type;

      using key_value_index = chainbase::shared_multi_index_container<
         key_value_object,
         indexed_by<
            ordered_unique<tag<by_id>, member<key_value_object, object_id_type, &key_value_object::id>>,
            ordered_unique<tag<by_scope_primary>,
               composite_key< key_value_object,
                  member<key_value_object, TableIdType, &key_value_object::t_id>,
                  member<key_value_object, uint64_t, &key_value_object::primary_key>
               >,
               composite_key_compare< std::less<TableIdType>, std::less<uint64_t> >
            >
         >
      >;
   };

   typedef key_value_index_container_base<table_id, key_value_object_type>    key_value_index_container;
   typedef key_value_index_container::key_value_object                        key_value_object;
   typedef key_value_index_container::key_value_index                         key_value_index;

   typedef key_value_index_container_base<shared_table_id, shared_key_value_object_type>     shared_key_value_index_container;
   typedef shared_key_value_index_container::key_value_object                                shared_key_value_object;
   typedef shared_key_value_index_container::key_value_index                                 shared_key_value_index;

   struct by_primary;
   struct by_secondary;

   template<typename TableIdType, uint64_t ObjectTypeId, typename SecondaryKey, typename SecondaryKeyLess = std::less<SecondaryKey> >
   struct secondary_index_base
   {
      struct index_object : public chainbase::object<ObjectTypeId,index_object> {
         OBJECT_CTOR(index_object)
         typedef SecondaryKey secondary_key_type;
         typedef typename chainbase::object<ObjectTypeId,index_object>::id_type object_id_type;

         object_id_type       id;
         TableIdType   t_id; //< t_id should not be changed within a chainbase modifier lambda
         uint64_t      primary_key; //< primary_key should not be changed within a chainbase modifier lambda
         account_name  payer;
         SecondaryKey  secondary_key; //< secondary_key should not be changed within a chainbase modifier lambda
      };

      typedef typename index_object::id_type     object_id_type;

      typedef chainbase::shared_multi_index_container<
         index_object,
         indexed_by<
            ordered_unique<tag<by_id>, member<index_object, object_id_type, &index_object::id>>,
            ordered_unique<tag<by_primary>,
               composite_key< index_object,
                  member<index_object, TableIdType, &index_object::t_id>,
                  member<index_object, uint64_t, &index_object::primary_key>
               >,
               composite_key_compare< std::less<TableIdType>, std::less<uint64_t> >
            >,
            ordered_unique<tag<by_secondary>,
               composite_key< index_object,
                  member<index_object, TableIdType, &index_object::t_id>,
                  member<index_object, SecondaryKey, &index_object::secondary_key>,
                  member<index_object, uint64_t, &index_object::primary_key>
               >,
               composite_key_compare< std::less<TableIdType>, SecondaryKeyLess, std::less<uint64_t> >
            >
         >
      > index_index;
   };

   using key256_t                            = std::array<uint128_t, 2>;
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

   template<uint64_t ObjectTypeId, typename SecondaryKey, typename SecondaryKeyLess = std::less<SecondaryKey> >
   using secondary_index = secondary_index_base<table_id, ObjectTypeId, SecondaryKey, SecondaryKeyLess>;

   using index64_index_container             = secondary_index<index64_object_type, uint64_t>;
   using index64_object                      =  index64_index_container::index_object;
   using index64_index                       =  index64_index_container::index_index;

   using index128_index_container            = secondary_index<index128_object_type, uint128_t>;
   using index128_object                     =  index128_index_container::index_object;
   using index128_index                      =  index128_index_container::index_index;

   using index256_index_container            = secondary_index<index256_object_type, key256_t>;
   using index256_object                     =  index256_index_container::index_object;
   using index256_index                      =  index256_index_container::index_index;

   /**
    *  This index supports a deterministic software implementation of double as the secondary key.
    *
    *  The software double implementation is using the Berkeley softfloat library (release 3).
    */
   using index_double_index_container        = secondary_index<index_double_object_type, float64_t, soft_double_less>;
   using index_double_object                 =  index_double_index_container::index_object;
   using index_double_index                  =  index_double_index_container::index_index;

   /**
    *  This index supports a deterministic software implementation of long double as the secondary key.
    *
    *  The software long double implementation is using the Berkeley softfloat library (release 3).
    */
   using index_long_double_index_container   = secondary_index<index_long_double_object_type, float128_t, soft_long_double_less>;
   using index_long_double_object            =  index_long_double_index_container::index_object;
   using index_long_double_index             =  index_long_double_index_container::index_index;

   template<uint64_t ObjectTypeId, typename SecondaryKey, typename SecondaryKeyLess = std::less<SecondaryKey> >
   using shared_secondary_index = secondary_index_base<shared_table_id, ObjectTypeId, SecondaryKey, SecondaryKeyLess>;

   using shared_index64_index_container            = shared_secondary_index<shared_index64_object_type, uint64_t>;
   using shared_index64_object                     = shared_index64_index_container::index_object;
   using shared_index64_index                      = shared_index64_index_container::index_index;

   using shared_index128_index_container           = shared_secondary_index<shared_index128_object_type, uint128_t>;
   using shared_index128_object                    = shared_index128_index_container::index_object;
   using shared_index128_index                     = shared_index128_index_container::index_index;

   using shared_index256_index_container           = shared_secondary_index<shared_index256_object_type, key256_t>;
   using shared_index256_object                    = shared_index256_index_container::index_object;
   using shared_index256_index                     = shared_index256_index_container::index_index;

   /**
    *  This index supports a deterministic software implementation of double as the secondary key.
    *
    *  The software double implementation is using the Berkeley softfloat library (release 3).
    */
   using shared_index_double_index_container       = shared_secondary_index<shared_index_double_object_type, float64_t, soft_double_less>;
   using shared_index_double_object                =  shared_index_double_index_container::index_object;
   using shared_index_double_index                 =  shared_index_double_index_container::index_index;

   /**
    *  This index supports a deterministic software implementation of long double as the secondary key.
    *
    *  The software long double implementation is using the Berkeley softfloat library (release 3).
    */
   using shared_index_long_double_index_container  = shared_secondary_index<shared_index_long_double_object_type, float128_t, soft_long_double_less>;
   using shared_index_long_double_object           =  shared_index_long_double_index_container::index_object;
   using shared_index_long_double_index            =  shared_index_long_double_index_container::index_index;


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
    * to use when traversing by table_id
    */
   template<typename T>
   struct object_to_table_id_tag;

#define DECLARE_TABLE_ID_TAG( object, tag ) \
   template<> \
   struct object_to_table_id_tag<object> { \
      using tag_type = tag;\
   };

   DECLARE_TABLE_ID_TAG(key_value_object, by_scope_primary)
   DECLARE_TABLE_ID_TAG(index64_object, by_primary)
   DECLARE_TABLE_ID_TAG(index128_object, by_primary)
   DECLARE_TABLE_ID_TAG(index256_object, by_primary)
   DECLARE_TABLE_ID_TAG(index_double_object, by_primary)
   DECLARE_TABLE_ID_TAG(index_long_double_object, by_primary)

   DECLARE_TABLE_ID_TAG(shared_key_value_object, by_scope_primary)
   DECLARE_TABLE_ID_TAG(shared_index64_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index128_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index256_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index_double_object, by_primary)
   DECLARE_TABLE_ID_TAG(shared_index_long_double_object, by_primary)

   template<typename T>
   using object_to_table_id_tag_t = typename object_to_table_id_tag<T>::tag_type;

   struct contract_tables {
      typedef eosio::chain::table_id_object table_id_object;
      typedef eosio::chain::key_value_object key_value_object;
      typedef eosio::chain::index64_object   index64_object;
      typedef eosio::chain::index128_object  index128_object;
      typedef eosio::chain::index256_object  index256_object;
      typedef eosio::chain::index_double_object index_double_object;
      typedef eosio::chain::index_long_double_object index_long_double_object;
   };

   struct contract_shared_tables {
      using table_id_object = shared_table_id_object;
      using key_value_object = shared_key_value_object;
      using index64_object = shared_index64_object;
      using index128_object = shared_index128_object;
      using index256_object = shared_index256_object;
      using index_double_object = shared_index_double_object;
      using index_long_double_object = shared_index_long_double_object;
   };

namespace config {
   template<>
   struct billable_size<table_id_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2;  ///< overhead for 2x indices internal-key and code,scope,table
      static const uint64_t value = 44 + overhead; ///< 44 bytes for constant size fields + overhead
   };

   template<>
   struct billable_size<key_value_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2;  ///< overhead for potentially single-row table, 2x indices internal-key and primary key
      static const uint64_t value = 32 + 8 + 4 + overhead; ///< 32 bytes for our constant size fields, 8 for pointer to vector data, 4 bytes for a size of vector + overhead
   };

   template<>
   struct billable_size<index64_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 8 + overhead; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<index128_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 16 + overhead; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

   template<>
   struct billable_size<index256_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 32 + overhead; ///< 24 bytes for fixed fields + 32 bytes key + overhead
   };

   template<>
   struct billable_size<index_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 8 + overhead; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<index_long_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 16 + overhead; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

   template<>
   struct billable_size<shared_table_id_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2 * shared_contract_bytes_multiplier;  ///< overhead for 2x indices internal-key and code,scope,table
      static const uint64_t value = (44 + overhead) * shared_contract_bytes_multiplier; ///< 44 bytes for constant size fields + overhead
   };

   template<>
   struct billable_size<shared_key_value_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 2 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 2x indices internal-key and primary key
      static const uint64_t value = (32 + 8 + 4 + overhead) * shared_contract_bytes_multiplier; ///< 32 bytes for our constant size fields, 8 for pointer to vector data, 4 bytes for a size of vector + overhead
   };

   template<>
   struct billable_size<shared_index64_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = (24 + 8 + overhead) * shared_contract_bytes_multiplier; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index128_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = (24 + 16 + overhead) * shared_contract_bytes_multiplier; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index256_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = 24 + 32 + overhead; ///< 24 bytes for fixed fields + 32 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = (24 + 8 + overhead) * shared_contract_bytes_multiplier; ///< 24 bytes for fixed fields + 8 bytes key + overhead
   };

   template<>
   struct billable_size<shared_index_long_double_object> {
      static const uint64_t overhead = overhead_per_row_per_index_ram_bytes * 3 * shared_contract_bytes_multiplier;  ///< overhead for potentially single-row table, 3x indices internal-key, primary key and primary+secondary key
      static const uint64_t value = (24 + 16 + overhead) * shared_contract_bytes_multiplier; ///< 24 bytes for fixed fields + 16 bytes key + overhead
   };

} // namespace config

} }  // namespace eosio::chain

CHAINBASE_SET_INDEX_TYPE(eosio::chain::table_id_object, eosio::chain::table_id_multi_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::key_value_object, eosio::chain::key_value_index)

CHAINBASE_SET_INDEX_TYPE(eosio::chain::index64_object, eosio::chain::index64_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::index128_object, eosio::chain::index128_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::index256_object, eosio::chain::index256_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::index_double_object, eosio::chain::index_double_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::index_long_double_object, eosio::chain::index_long_double_index)

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_table_id_object, eosio::chain::shared_table_id_multi_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_key_value_object, eosio::chain::shared_key_value_index)

CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index64_object, eosio::chain::shared_index64_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index128_object, eosio::chain::shared_index128_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index256_object, eosio::chain::shared_index256_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index_double_object, eosio::chain::shared_index_double_index)
CHAINBASE_SET_INDEX_TYPE(eosio::chain::shared_index_long_double_object, eosio::chain::shared_index_long_double_index)



FC_REFLECT(eosio::chain::table_id_object, (code)(scope)(table)(payer)(count) )
FC_REFLECT(eosio::chain::key_value_object, (primary_key)(payer)(value) )

FC_REFLECT(eosio::chain::shared_table_id_object, (code)(scope)(table)(payer)(count) )
FC_REFLECT(eosio::chain::shared_key_value_object, (primary_key)(payer)(value) )

#define REFLECT_SECONDARY(type)\
  FC_REFLECT(type, (primary_key)(payer)(secondary_key) )

REFLECT_SECONDARY(eosio::chain::index64_object)
REFLECT_SECONDARY(eosio::chain::index128_object)
REFLECT_SECONDARY(eosio::chain::index256_object)
REFLECT_SECONDARY(eosio::chain::index_double_object)
REFLECT_SECONDARY(eosio::chain::index_long_double_object)

REFLECT_SECONDARY(eosio::chain::shared_index64_object)
REFLECT_SECONDARY(eosio::chain::shared_index128_object)
REFLECT_SECONDARY(eosio::chain::shared_index256_object)
REFLECT_SECONDARY(eosio::chain::shared_index_double_object)
REFLECT_SECONDARY(eosio::chain::shared_index_long_double_object)