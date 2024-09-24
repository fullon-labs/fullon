#pragma once

#include <eosio/chain/database_utils.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/variant_object.hpp>
#include <boost/core/demangle.hpp>
#include <ostream>
#include <memory>

namespace eosio { namespace chain {
   /**
    * History:
    * Version 1: initial version with string identified sections and rows
    */
   static const uint32_t current_snapshot_version = 1;

   namespace detail {
      template<typename T>
      struct snapshot_section_traits {
         static std::string section_name() {
            return boost::core::demangle(typeid(T).name());
         }
      };

      template<typename T>
      struct snapshot_row_traits {
         using value_type = std::decay_t<T>;
         using snapshot_type = value_type;

         static const snapshot_type& to_snapshot_row( const value_type& value, const chainbase::database& ) {
            return value;
         };
      };

      /**
       * Due to a pattern in our code of overloading `operator << ( std::ostream&, ... )` to provide
       * human-readable string forms of data, we cannot directly use ostream as those operators will
       * be used instead of the expected operators.  In otherwords:
       * fc::raw::pack(fc::datastream...)
       * will end up calling _very_ different operators than
       * fc::raw::pack(std::ostream...)
       */
      struct ostream_wrapper {
         explicit ostream_wrapper(std::ostream& s)
         :inner(s) {

         }

         ostream_wrapper(ostream_wrapper &&) = default;
         ostream_wrapper(const ostream_wrapper& ) = default;

         auto& write( const char* d, size_t s ) {
            return inner.write(d, s);
         }

         auto& put(char c) {
           return inner.put(c);
         }

         auto tellp() const {
            return inner.tellp();
         }

         auto& seekp(std::ostream::pos_type p) {
            return inner.seekp(p);
         }

         std::ostream& inner;
      };


      struct abstract_snapshot_row_writer {
         virtual void write(ostream_wrapper& out) const = 0;
         virtual void write(fc::sha256::encoder& out) const = 0;
         virtual fc::variant to_variant() const = 0;
         virtual std::string row_type_name() const = 0;
      };

      template<typename T>
      struct snapshot_row_writer : abstract_snapshot_row_writer {
         explicit snapshot_row_writer( const T& data )
         :data(data) {}

         template<typename DataStream>
         void write_stream(DataStream& out) const {
            fc::raw::pack(out, data);
         }

         void write(ostream_wrapper& out) const override {
            write_stream(out);
         }

         void write(fc::sha256::encoder& out) const override {
            write_stream(out);
         }

         fc::variant to_variant() const override {
            fc::variant var;
            fc::to_variant(data, var);
            return var;
         }

         std::string row_type_name() const override {
            return boost::core::demangle( typeid( T ).name() );
         }

         const T& data;
      };

      template<typename T>
      snapshot_row_writer<T> make_row_writer( const T& data) {
         return snapshot_row_writer<T>(data);
      }
   }

   class snapshot_shard_writer {
      public:
         class section_writer {
            public:
               template<typename T>
               void add_row( const T& row, const chainbase::database& db ) {
                  _writer.write_row(detail::make_row_writer(detail::snapshot_row_traits<T>::to_snapshot_row(row, db)));
               }

            private:
               friend class snapshot_shard_writer;
               section_writer(snapshot_shard_writer& writer)
               :_writer(writer)
               {

               }
               snapshot_shard_writer& _writer;
         };

         template<typename F>
         void write_section(const std::string section_name, F f) {
            write_start_section(section_name);
            auto section = section_writer(*this);
            f(section);
            write_end_section();
         }

         template<typename T, typename F>
         void write_section(F f) {
            write_section(detail::snapshot_section_traits<T>::section_name(), f);
         }

         snapshot_shard_writer(const chain::shard_name& shard_name): shard_name(shard_name){};

         virtual ~snapshot_shard_writer(){};

         chain::shard_name shard_name;
      protected:
         virtual void write_start_section( const std::string& section_name ) = 0;
         virtual void write_row( const detail::abstract_snapshot_row_writer& row_writer ) = 0;
         virtual void write_end_section() = 0;
   };
   using snapshot_shard_writer_ptr = std::shared_ptr<snapshot_shard_writer>;

   class snapshot_writer {
      public:
         template<typename F>
         void add_shard( const chain::shard_name& shard_name, F f ) {
            snapshot_shard_writer_ptr shard_writer = add_shard_start(shard_name);
            f(shard_writer);
            add_shard_end(shard_name);
         }

         virtual ~snapshot_writer(){};
      protected:
         virtual snapshot_shard_writer_ptr add_shard_start( const chain::shard_name& shard_name ) = 0;
         virtual void add_shard_end(const chain::shard_name& shard_name) = 0;
   };

   using snapshot_writer_ptr = std::shared_ptr<snapshot_writer>;

   namespace detail {
      struct abstract_snapshot_row_reader {
         virtual void provide(std::istream& in) const = 0;
         virtual void provide(const fc::variant&) const = 0;
         virtual std::string row_type_name() const = 0;
      };

      template<typename T>
      struct is_chainbase_object {
         static constexpr bool value = false;
      };

      template<uint16_t TypeNumber, typename Derived>
      struct is_chainbase_object<chainbase::object<TypeNumber, Derived>> {
         static constexpr bool value = true;
      };

      template<typename T>
      constexpr bool is_chainbase_object_v = is_chainbase_object<T>::value;

      struct row_validation_helper {
         template<typename T, typename F>
         static auto apply(const T& data, F f) -> std::enable_if_t<is_chainbase_object_v<T>> {
            auto orig = data.id;
            f();
            EOS_ASSERT(orig == data.id, snapshot_exception,
                       "Snapshot for ${type} mutates row member \"id\" which is illegal",
                       ("type",boost::core::demangle( typeid( T ).name() )));
         }

         template<typename T, typename F>
         static auto apply(const T&, F f) -> std::enable_if_t<!is_chainbase_object_v<T>> {
            f();
         }
      };

      template<typename T>
      struct snapshot_row_reader : abstract_snapshot_row_reader {
         explicit snapshot_row_reader( T& data )
         :data(data) {}


         void provide(std::istream& in) const override {
            row_validation_helper::apply(data, [&in,this](){
               fc::raw::unpack(in, data);
            });
         }

         void provide(const fc::variant& var) const override {
            row_validation_helper::apply(data, [&var,this]() {
               fc::from_variant(var, data);
            });
         }

         std::string row_type_name() const override {
            return boost::core::demangle( typeid( T ).name() );
         }

         T& data;
      };

      template<typename T>
      snapshot_row_reader<T> make_row_reader( T& data ) {
         return snapshot_row_reader<T>(data);
      }
   }

   class snapshot_shard_reader {
      public:
         class section_reader {
            public:
               template<typename T>
               auto read_row( T& out ) -> std::enable_if_t<std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  auto reader = detail::make_row_reader(out);
                  return _reader.read_row(reader);
               }

               template<typename T>
               auto read_row( T& out, chainbase::database& ) -> std::enable_if_t<std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  return read_row(out);
               }

               template<typename T>
               auto read_row( T& out, chainbase::database& db ) -> std::enable_if_t<!std::is_same<std::decay_t<T>, typename detail::snapshot_row_traits<T>::snapshot_type>::value,bool> {
                  auto temp = typename detail::snapshot_row_traits<T>::snapshot_type();
                  auto reader = detail::make_row_reader(temp);
                  bool result = _reader.read_row(reader);
                  detail::snapshot_row_traits<T>::from_snapshot_row(std::move(temp), out, db);
                  return result;
               }

               bool empty() {
                  return _reader.empty();
               }

            private:
               friend class snapshot_shard_reader;
               section_reader(snapshot_shard_reader& _reader)
               :_reader(_reader)
               {}

               snapshot_shard_reader& _reader;
         };

         template<typename F>
         void read_section(const std::string& section_name, F f) {
            set_section(section_name);
            auto section = section_reader(*this);
            f(section);
            clear_section();
         }

         template<typename T, typename F>
         void read_section(F f) {
            read_section(detail::snapshot_section_traits<T>::section_name(), f);
         }

         snapshot_shard_reader(const chain::shard_name& shard_name): shard_name(shard_name) {};
         virtual ~snapshot_shard_reader(){};

         chain::shard_name shard_name;
      protected:
         virtual void set_section( const std::string& section_name ) = 0;
         virtual bool read_row( detail::abstract_snapshot_row_reader& row_reader ) = 0;
         virtual bool empty( ) = 0;
         virtual void clear_section() = 0;
   };

   using snapshot_shard_reader_ptr = std::shared_ptr<snapshot_shard_reader>;

   class snapshot_reader {
      public:
         template<typename F>
         void read_shard(const chain::shard_name& shard_name, F f) {
            snapshot_shard_reader_ptr shard = read_shard_start(shard_name);
            f(shard);
            read_shard_end();
         }

         virtual void validate() const = 0;

         virtual void return_to_header() = 0;

         virtual ~snapshot_reader(){};

      protected:
         virtual snapshot_shard_reader_ptr read_shard_start( const chain::shard_name& shard_name ) = 0;
         virtual void read_shard_end() = 0;
   };

   using snapshot_reader_ptr = std::shared_ptr<snapshot_reader>;

   class variant_snapshot_shard_writer : public snapshot_shard_writer {
      public:
         variant_snapshot_shard_writer(const chain::shard_name& shard_name);

         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

         fc::mutable_variant_object snapshot_shard;
      private:
         fc::variants *sections = nullptr;
         std::string current_section_name;
         fc::variants current_rows;
   };

   using variant_snapshot_shard_writer_ptr = std::shared_ptr<variant_snapshot_shard_writer>;

   class variant_snapshot_writer : public snapshot_writer {
      public:
         variant_snapshot_writer(fc::mutable_variant_object& snapshot);

         snapshot_shard_writer_ptr add_shard_start( const chain::shard_name& shard_name ) override;
         void add_shard_end(const chain::shard_name& shard_name) override;

         void finalize();

      private:
         friend class variant_snapshot_shard_writer;

         fc::mutable_variant_object& snapshot;

         fc::mutable_variant_object shards;
         variant_snapshot_shard_writer_ptr current_shard;
   };

   class variant_snapshot_shard_reader : public snapshot_shard_reader {
      public:
         explicit variant_snapshot_shard_reader(const chain::shard_name& shard_name, const fc::variant_object& shard);

         void validate() const;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;

      private:
         const fc::variant_object& shard;
         const fc::variants *sections = nullptr;
         const fc::variant_object* cur_section = nullptr;
         uint64_t cur_row = 0;
   };

   using variant_snapshot_shard_reader_ptr = std::shared_ptr<variant_snapshot_shard_reader>;


   class variant_snapshot_reader : public snapshot_reader {
      public:
         explicit variant_snapshot_reader(const fc::variant& snapshot);

         void validate() const override;
         void return_to_header() override;
      protected:
         snapshot_shard_reader_ptr read_shard_start( const chain::shard_name& shard_name ) override;
         void read_shard_end() override;
      private:
         const fc::variant& snapshot;
         const fc::variant_object *shards = nullptr;
         variant_snapshot_shard_reader_ptr cur_shard;

         void validate_sections(const fc::variant_object& snapshot) const;
   };

   class ostream_snapshot_shard_writer : public snapshot_shard_writer {
      public:
         explicit ostream_snapshot_shard_writer(const chain::shard_name& shard_name, detail::ostream_wrapper& snapshot);

         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

         static const uint32_t magic_number = 0x30510550;

      private:
         detail::ostream_wrapper snapshot;
         std::streampos          shard_pos = -1;
         uint64_t                section_count = 0;
         std::streampos          section_pos = -1;
         uint64_t                row_count = 0;
   };

   using ostream_snapshot_shard_writer_ptr = std::shared_ptr<ostream_snapshot_shard_writer>;

   class ostream_snapshot_writer : public snapshot_writer {
      public:
         explicit ostream_snapshot_writer(std::ostream& snapshot);

         void finalize();

         static const uint32_t magic_number = 0x30510550;

      protected:
         snapshot_shard_writer_ptr add_shard_start( const chain::shard_name& shard_name ) override;
         void add_shard_end(const chain::shard_name& shard_name) override;
      private:
         detail::ostream_wrapper snapshot;
         std::streampos          header_pos = -1;
         std::streampos          shards_pos = -1;
         uint64_t                shard_count = 0;
         ostream_snapshot_shard_writer_ptr cur_shard;
         // std::streampos          shard_pos = -1;
   };

   class ostream_json_snapshot_writer;

   class ostream_json_snapshot_shard_writer : public snapshot_shard_writer {
      public:
         explicit ostream_json_snapshot_shard_writer(const chain::shard_name& shard_name, detail::ostream_wrapper& snapshot);

         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section() override;
         void finalize();

         static const uint32_t magic_number = 0x30510550;

      private:
         detail::ostream_wrapper& snapshot;
         uint64_t                 section_count = 0;
         uint64_t                 row_count = 0;
   };

   using ostream_json_snapshot_shard_writer_ptr = std::shared_ptr<ostream_json_snapshot_shard_writer>;

   class ostream_json_snapshot_writer : public snapshot_writer {
      public:
         explicit ostream_json_snapshot_writer(std::ostream& snapshot);

         void finalize();

         static const uint32_t magic_number = 0x30510550;
      protected:
         snapshot_shard_writer_ptr add_shard_start( const chain::shard_name& shard_name ) override;
         void add_shard_end(const chain::shard_name& shard_name) override;
      private:
         detail::ostream_wrapper snapshot;
         uint64_t                shard_count = 0;
         ostream_json_snapshot_shard_writer_ptr cur_shard;
   };

   class istream_snapshot_shard_reader : public snapshot_shard_reader {
      public:
         struct shard_info {
            std::streampos shard_pos = -1;
            uint64_t shard_size = 0;
            uint64_t section_count = 0;
            chain::shard_name shard_name;

            std::streampos get_first_section_pos() const {
               return shard_pos + std::streampos(sizeof(shard_size) + sizeof(section_count) + sizeof(uint64_t));
            }
         };

         struct section_info {
            std::streampos section_pos = -1;
            uint64_t section_size = 0;
            uint64_t row_count = 0;
            uint32_t section_name_size = 0;
            std::string section_name = 0;
            std::streampos get_first_row_pos() const {
               return section_pos + std::streampos(sizeof(section_size) + sizeof(row_count) + sizeof(section_name_size) + section_name_size);
            }
         };

         explicit istream_snapshot_shard_reader(const chain::shard_name& shard_name, std::istream& snapshot, const shard_info& shard);

         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;
      private:
         void init_sections();

         std::istream&  snapshot;
         const shard_info& shard;
         std::map<std::string, section_info> sections;
         std::map<std::string, section_info>::const_iterator cur_section_itr;
         std::streampos header_pos;
         uint64_t       num_rows;
         uint64_t       cur_row;
   };
   using istream_snapshot_shard_reader_ptr = std::shared_ptr<istream_snapshot_shard_reader>;


   class istream_snapshot_reader : public snapshot_reader {
      public:
         explicit istream_snapshot_reader(std::istream& snapshot);

         void validate() const override;
         void return_to_header() override;

      protected:
         snapshot_shard_reader_ptr read_shard_start( const chain::shard_name& shard_name ) override;
         void read_shard_end() override;
      private:
         typedef istream_snapshot_shard_reader::shard_info shard_info;

         void validate_shard() const;
         void validate_section() const;
         void init_shards();

         std::istream&  snapshot;
         std::streampos header_pos;
         std::map<chain::shard_name, shard_info> shards;
         bool is_shards_init = false;
         istream_snapshot_shard_reader_ptr cur_shard;
   };

   class istream_json_snapshot_shard_reader : public snapshot_shard_reader {
      public:
         typedef std::shared_ptr<struct istream_json_snapshot_shard_reader_impl> impl_type_ptr;
         explicit istream_json_snapshot_shard_reader(const chain::shard_name& shard_name, impl_type_ptr impl);
         ~istream_json_snapshot_shard_reader();

         // void validate() const override;
         void set_section( const string& section_name ) override;
         bool read_row( detail::abstract_snapshot_row_reader& row_reader ) override;
         bool empty ( ) override;
         void clear_section() override;

      private:
         bool validate_section() const;

         impl_type_ptr impl;
   };


   class istream_json_snapshot_reader : public snapshot_reader {
      public:
         explicit istream_json_snapshot_reader(const fc::path& p);
         ~istream_json_snapshot_reader();

         void validate() const override;
         void return_to_header() override;

      protected:
         snapshot_shard_reader_ptr read_shard_start( const chain::shard_name& shard_name ) override;
         void read_shard_end() override;
      private:
         bool validate_section() const;

         std::unique_ptr<struct istream_json_snapshot_reader_impl> impl;
         std::shared_ptr<istream_json_snapshot_shard_reader> cur_shard;
   };

   class integrity_hash_snapshot_shard_writer : public snapshot_shard_writer {
      public:
         explicit integrity_hash_snapshot_shard_writer(const chain::shard_name& shard_name, fc::sha256::encoder& enc);

         void write_start_section( const std::string& section_name ) override;
         void write_row( const detail::abstract_snapshot_row_writer& row_writer ) override;
         void write_end_section( ) override;
         void finalize();

      private:
         fc::sha256::encoder&  enc;
   };

   using integrity_hash_snapshot_shard_writer_ptr = std::shared_ptr<integrity_hash_snapshot_shard_writer>;

   class integrity_hash_snapshot_writer : public snapshot_writer {
      public:
         explicit integrity_hash_snapshot_writer(fc::sha256::encoder&  enc);

         void finalize();
      protected:
         snapshot_shard_writer_ptr add_shard_start( const chain::shard_name& shard_name ) override;
         void add_shard_end(const chain::shard_name& shard_name) override;
      private:
         fc::sha256::encoder&  enc;
         integrity_hash_snapshot_shard_writer_ptr cur_shard;

   };

}}
