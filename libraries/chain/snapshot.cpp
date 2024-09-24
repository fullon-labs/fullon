#define RAPIDJSON_NAMESPACE eosio_rapidjson // This is ABSOLUTELY necessary anywhere that is using eosio_rapidjson

#include <eosio/chain/snapshot.hpp>
#include <eosio/chain/exceptions.hpp>
#include <fc/scoped_exit.hpp>
#include <fc/io/json.hpp>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using namespace eosio_rapidjson;

namespace eosio { namespace chain {

variant_snapshot_shard_writer::variant_snapshot_shard_writer(const chain::shard_name& shard_name)
:snapshot_shard_writer(shard_name)
{
   snapshot_shard.set("sections", fc::variants());

   sections = &snapshot_shard["sections"].get_array();
}

void variant_snapshot_shard_writer::write_start_section( const std::string& section_name ) {
   current_rows.clear();
   current_section_name = section_name;
}

void variant_snapshot_shard_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   current_rows.emplace_back(row_writer.to_variant());
}

void variant_snapshot_shard_writer::write_end_section( ) {
   sections->emplace_back(fc::mutable_variant_object()("name", std::move(current_section_name))("rows", std::move(current_rows)));
}

void variant_snapshot_shard_writer::finalize() {

}

variant_snapshot_writer::variant_snapshot_writer(fc::mutable_variant_object& snapshot)
: snapshot(snapshot)
{
   snapshot.set("version", current_snapshot_version );
}

snapshot_shard_writer_ptr variant_snapshot_writer::add_shard_start( const chain::shard_name& shard_name ) {
   current_shard = std::make_shared<variant_snapshot_shard_writer>(shard_name);
   return current_shard;
}

void variant_snapshot_writer::add_shard_end(const chain::shard_name& shard_name) {
   shards(shard_name.to_string(), std::move(current_shard->snapshot_shard));
}

void variant_snapshot_writer::finalize() {
   snapshot.set("shards", std::move(shards));
}

variant_snapshot_shard_reader::variant_snapshot_shard_reader(const chain::shard_name& shard_name, const fc::variant_object& shard)
:snapshot_shard_reader(shard_name)
,shard(shard)
{}

void variant_snapshot_shard_reader::set_section( const string& section_name ) {
   if (sections == nullptr) {
      sections = &shard["sections"].get_array();
   }
   for( const auto& section: *sections ) {
      if (section["name"].as_string() == section_name) {
         cur_section = &section.get_object();
         return;
      }
   }

   EOS_THROW(snapshot_exception, "Variant snapshot has no section named ${n}", ("n", section_name));
}

bool variant_snapshot_shard_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   const auto& rows = (*cur_section)["rows"].get_array();
   row_reader.provide(rows.at(cur_row++));
   return cur_row < rows.size();
}

bool variant_snapshot_shard_reader::empty ( ) {
   const auto& rows = (*cur_section)["rows"].get_array();
   return rows.empty();
}

void variant_snapshot_shard_reader::clear_section() {
   cur_section = nullptr;
   cur_row = 0;
}

variant_snapshot_reader::variant_snapshot_reader(const fc::variant& snapshot)
:snapshot(snapshot)
{}

void variant_snapshot_reader::validate() const {
   EOS_ASSERT(snapshot.is_object(), snapshot_validation_exception,
         "Variant snapshot is not an object");
   const fc::variant_object& o = snapshot.get_object();

   EOS_ASSERT(o.contains("version"), snapshot_validation_exception,
         "Variant snapshot has no version");

   const auto& version = o["version"];
   EOS_ASSERT(version.is_integer(), snapshot_validation_exception,
         "Variant snapshot version is not an integer");

   EOS_ASSERT(version.as_uint64() == (uint64_t)current_snapshot_version, snapshot_validation_exception,
         "Variant snapshot is an unsuppored version.  Expected : ${expected}, Got: ${actual}",
         ("expected", current_snapshot_version)("actual",o["version"].as_uint64()));


   EOS_ASSERT(o.contains("shards"), snapshot_validation_exception,
         "Variant snapshot has no sections");

   const auto& shards = snapshot["shards"];
   EOS_ASSERT(shards.is_object(), snapshot_validation_exception,  "Variant snapshot shards is not an object");

   const auto& shards_obj = shards.get_object();
   for( const auto& shard: shards_obj ) {
      const auto& shard_name = chain::shard_name(shard.key());
      const auto& shard_value = shard.value();

      EOS_ASSERT(!shard_name.empty(), snapshot_validation_exception, "Variant snapshot shard name invalid");

      EOS_ASSERT(shard_value.is_object(), snapshot_validation_exception, "Variant snapshot shard is not an object");

      const auto& so = shard_value.get_object();

      validate_sections(so);
   }
   // TODO: shard count verify
}


void variant_snapshot_reader::validate_sections(const fc::variant_object& snapshot) const {

   EOS_ASSERT(snapshot.contains("sections"), snapshot_validation_exception,
         "Variant snapshot shard has no sections");

   const auto& sections = snapshot["sections"];
   EOS_ASSERT(sections.is_array(), snapshot_validation_exception, "Variant snapshot shard sections is not an array");

   const auto& section_array = sections.get_array();
   for( const auto& section: section_array ) {
      EOS_ASSERT(section.is_object(), snapshot_validation_exception, "Variant snapshot shard section is not an object");

      const auto& so = section.get_object();
      EOS_ASSERT(so.contains("name"), snapshot_validation_exception,
            "Variant snapshot shard section has no name");

      EOS_ASSERT(so["name"].is_string(), snapshot_validation_exception,
                 "Variant snapshot shard section name is not a string");

      EOS_ASSERT(so.contains("rows"), snapshot_validation_exception,
                 "Variant snapshot shard section has no rows");

      EOS_ASSERT(so["rows"].is_array(), snapshot_validation_exception,
                 "Variant snapshot shard section rows is not an array");
   }
}

snapshot_shard_reader_ptr variant_snapshot_reader::read_shard_start( const chain::shard_name& shard_name ) {
   if (shards == nullptr) {
      shards = &snapshot["shards"].get_object();
   }

   auto itr = shards->find(shard_name.to_string());
   EOS_ASSERT(itr != shards->end(), snapshot_exception,
               "Variant snapshot has no shard named ${n}", ("n", shard_name));
   const auto& so = itr->value().get_object();
   cur_shard = std::make_shared<variant_snapshot_shard_reader>(shard_name, so);
   return cur_shard;
}

void variant_snapshot_reader::read_shard_end() {
   cur_shard.reset();
}

void variant_snapshot_reader::return_to_header() {
   cur_shard.reset();
}

ostream_snapshot_shard_writer::ostream_snapshot_shard_writer(const chain::shard_name& shard_name, detail::ostream_wrapper& snapshot)
:snapshot_shard_writer(shard_name)
,snapshot(snapshot)
,shard_pos(snapshot.tellp())
{

   uint64_t placeholder = std::numeric_limits<uint64_t>::max();

   // write a placeholder for the shard size
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write placeholder for sections count
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write the shard name value (uint64_t)
   auto shard_name_value = shard_name.to_uint64_t();
   snapshot.write((char*)&shard_name_value, sizeof(shard_name_value));
}

void ostream_snapshot_shard_writer::write_start_section( const std::string& section_name )
{
   EOS_ASSERT(section_pos == std::streampos(-1), snapshot_exception, "Attempting to write a new section without closing the previous section");
   section_pos = snapshot.tellp();
   row_count = 0;

   uint64_t placeholder = std::numeric_limits<uint64_t>::max();

   // write a placeholder for the section size
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write placeholder for row count
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write the section name (null terminated)
   uint32_t section_name_size = section_name.size();
   snapshot.write((char*)&section_name_size, sizeof(section_name_size));
   snapshot.write(section_name.data(), section_name.size());
}

void ostream_snapshot_shard_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   auto restore = snapshot.tellp();
   try {
      row_writer.write(snapshot);
   } catch (...) {
      snapshot.seekp(restore);
      throw;
   }
   row_count++;
}

void ostream_snapshot_shard_writer::write_end_section( ) {
   auto restore = snapshot.tellp();

   uint64_t section_size = restore - section_pos - sizeof(uint64_t);

   snapshot.seekp(section_pos);

   // write a the section size
   snapshot.write((char*)&section_size, sizeof(section_size));

   // write the row count
   snapshot.write((char*)&row_count, sizeof(row_count));

   snapshot.seekp(restore);

   section_pos = std::streampos(-1);
   row_count = 0;
}

void ostream_snapshot_shard_writer::finalize() {

   auto restore = snapshot.tellp();
   uint64_t shard_size = restore - shard_pos - sizeof(uint64_t);

   snapshot.seekp(shard_pos);

   // // write a the shard size
   snapshot.write((char*)&shard_size, sizeof(shard_size));

   // // write the section count
   snapshot.write((char*)&section_count, sizeof(section_count));

   snapshot.seekp(restore);

   shard_pos = std::streampos(-1);
   section_count = 0;
}

ostream_snapshot_writer::ostream_snapshot_writer(std::ostream& snapshot)
:snapshot(snapshot)
,header_pos(snapshot.tellp())
{
   // write magic number
   auto totem = magic_number;
   snapshot.write((char*)&totem, sizeof(totem));

   // write version
   auto version = current_snapshot_version;
   snapshot.write((char*)&version, sizeof(version));

   shards_pos = snapshot.tellp();
   uint64_t placeholder = std::numeric_limits<uint64_t>::max();
   // write a placeholder for the shards size
   snapshot.write((char*)&placeholder, sizeof(placeholder));

   // write placeholder for shard count
   snapshot.write((char*)&placeholder, sizeof(placeholder));
}

snapshot_shard_writer_ptr ostream_snapshot_writer::add_shard_start( const chain::shard_name& shard_name )
{
   EOS_ASSERT(!cur_shard, snapshot_exception, "Attempting to write a new section without closing the previous section");

   cur_shard = std::make_shared<ostream_snapshot_shard_writer>(shard_name, snapshot);
   shard_count++;
   return cur_shard;
}

void ostream_snapshot_writer::add_shard_end(const chain::shard_name& shard_name) {

   cur_shard->finalize();
   cur_shard.reset();
}

void ostream_snapshot_writer::finalize() {
   auto restore = snapshot.tellp();

   uint64_t shards_size = restore - shards_pos + sizeof(uint64_t);

   snapshot.seekp(shards_size);

   // write a the section size
   snapshot.write((char*)&shards_size, sizeof(shards_size));

   // write the row count
   snapshot.write((char*)&shard_count, sizeof(shard_count));

   snapshot.seekp(restore);

   uint64_t end_marker = std::numeric_limits<uint64_t>::max();
   // write a placeholder for the section size
   snapshot.write((char*)&end_marker, sizeof(end_marker));
}

ostream_json_snapshot_shard_writer::ostream_json_snapshot_shard_writer( const chain::shard_name& shard_name, detail::ostream_wrapper& snapshot)
:snapshot_shard_writer(shard_name)
,snapshot(snapshot)
,row_count(0)
{
   snapshot.inner << fc::json::to_string(shard_name.to_string(), fc::time_point::maximum()) << ":{\n\"sections\":{\n";
}

void ostream_json_snapshot_shard_writer::write_start_section( const std::string& section_name )
{
   row_count = 0;
   if (section_count == 0) {
      snapshot.inner << ",";
   }
   snapshot.inner << fc::json::to_string(section_name, fc::time_point::maximum()) << ":{\n\"rows\":[\n";
   section_count++;
}

void ostream_json_snapshot_shard_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   const auto yield = [&](size_t s) {};

   if(row_count != 0) snapshot.inner << ",";
   snapshot.inner << fc::json::to_string(row_writer.to_variant(), yield) << "\n";
   ++row_count;
}

void ostream_json_snapshot_shard_writer::write_end_section( ) {
   snapshot.inner << "],\n\"num_rows\":" << row_count << "\n}\n";
   row_count = 0;
}

void ostream_json_snapshot_shard_writer::finalize() {
   snapshot.inner << "},\n\"num_sections\":" << section_count << "\n}\n";
}

ostream_json_snapshot_writer::ostream_json_snapshot_writer(std::ostream& snapshot)
      :snapshot(snapshot)
      // ,row_count(0)
{
   snapshot << "{\n";
   // write magic number
   auto totem = magic_number;
   snapshot << "\"magic_number\":" << fc::json::to_string(totem, fc::time_point::maximum()) << "\n";

   // write version
   auto version = current_snapshot_version;
   snapshot << ",\"version\":" << fc::json::to_string(version, fc::time_point::maximum()) << "\n";
   snapshot << ",\"shards\":{\n";
}

snapshot_shard_writer_ptr ostream_json_snapshot_writer::add_shard_start( const chain::shard_name& shard_name )
{

   if(shard_count != 0) snapshot.inner << ",";
   cur_shard = std::make_shared<ostream_json_snapshot_shard_writer>(shard_name, snapshot);
   shard_count++;
   return cur_shard;
}

void ostream_json_snapshot_writer::add_shard_end(const chain::shard_name& shard_name) {
   cur_shard->finalize();
   cur_shard.reset();
}

void ostream_json_snapshot_writer::finalize() {
   snapshot.inner << "},\n\"num_shards\":" << shard_count << "\n"; // shards end
   snapshot.inner << "}\n"; // total end
   snapshot.inner.flush();
   shard_count = 0;
}

istream_snapshot_shard_reader::istream_snapshot_shard_reader(const chain::shard_name& shard_name,
                                                             std::istream& snapshot, const shard_info& shard)
:snapshot_shard_reader(shard_name)
,snapshot(snapshot)
,shard(shard)
,header_pos(snapshot.tellg())
,num_rows(0)
,cur_row(0)
{
   auto first_pos = shard.get_first_section_pos();
   snapshot.seekg(first_pos);

   uint64_t shard_count = 0;
   snapshot.read((char*)&shard_count, sizeof(shard_count));

   for (size_t i = 0; i < shard.section_count; i++) {
      section_info section;
      section.section_pos = snapshot.tellg();
      snapshot.read((char*)&section.section_size, sizeof(section.section_size));
      snapshot.read((char*)&section.row_count, sizeof(section.row_count));

      snapshot.read((char*)&section.section_name_size, sizeof(section.section_name_size));
      section.section_name.resize(section.section_name_size);
      for (auto& c : section.section_name) {
         c = snapshot.get();
      }
      sections[section.section_name] = section;
   }
   snapshot.seekg(first_pos);
}

void istream_snapshot_shard_reader::set_section( const string& section_name ) {
   cur_section_itr = sections.find(section_name);

   EOS_ASSERT(cur_section_itr != sections.end(), snapshot_exception,
               "Binary snapshot shard has no section named ${n}.", ("n", section_name));

   const auto& section = cur_section_itr->second;
   snapshot.seekg(section.get_first_row_pos());
}

bool istream_snapshot_shard_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   row_reader.provide(snapshot);
   assert(cur_section_itr != sections.end());
   return ++cur_row < cur_section_itr->second.row_count;
}

bool istream_snapshot_shard_reader::empty ( ) {
   return cur_section_itr == sections.end() || cur_section_itr->second.row_count == 0;
}

void istream_snapshot_shard_reader::clear_section() {
   cur_section_itr = sections.end();
   cur_row = 0;
}

istream_snapshot_reader::istream_snapshot_reader(std::istream& snapshot)
:snapshot(snapshot)
,header_pos(snapshot.tellg())
{}

void istream_snapshot_reader::validate() const {
   // make sure to restore the read pos
   auto restore_pos = fc::make_scoped_exit([this,pos=snapshot.tellg(),ex=snapshot.exceptions()](){
      snapshot.seekg(pos);
      snapshot.exceptions(ex);
   });

   snapshot.exceptions(std::istream::failbit|std::istream::eofbit);

   try {
      // validate totem
      auto expected_totem = ostream_snapshot_writer::magic_number;
      decltype(expected_totem) actual_totem;
      snapshot.read((char*)&actual_totem, sizeof(actual_totem));
      EOS_ASSERT(actual_totem == expected_totem, snapshot_exception,
                 "Binary snapshot has unexpected magic number!");

      // validate version
      auto expected_version = current_snapshot_version;
      decltype(expected_version) actual_version;
      snapshot.read((char*)&actual_version, sizeof(actual_version));
      EOS_ASSERT(actual_version == expected_version, snapshot_exception,
                 "Binary snapshot is an unsuppored version.  Expected : ${expected}, Got: ${actual}",
                 ("expected", expected_version)("actual", actual_version));

      uint64_t shards_size = 0;
      snapshot.read((char*)&shards_size, sizeof(shards_size));
      auto shards_pos = snapshot.tellg();
      uint64_t shard_count = 0;
      snapshot.read((char*)&shard_count, sizeof(shard_count));

      for (size_t i = 0; i < shard_count; i++)
      {
         validate_shard();
      }

      uint64_t actual_shards_size = snapshot.tellg() - shards_pos;
      EOS_ASSERT(actual_shards_size == shards_size, snapshot_exception,
                  "Binary snapshot shards size invalid.  Expected : ${expected}, Got: ${actual}",
                  ("expected", shards_size)("actual", actual_shards_size));
   } catch( const std::exception& e ) {  \
      snapshot_exception fce(FC_LOG_MESSAGE( warn, "Binary snapshot validation threw IO exception (${what})",("what",e.what())));
      throw fce;
   }
}

void istream_snapshot_reader::validate_shard() const {

   uint64_t shard_size = 0;
   snapshot.read((char*)&shard_size, sizeof(shard_size));

   auto shard_pos = snapshot.tellg();

   uint64_t section_count = 0;
   snapshot.read((char*)&section_count, sizeof(section_count));

   uint64_t shard_name_value = 0;
   snapshot.read((char*)&shard_name_value, sizeof(shard_name_value));

   EOS_ASSERT(name(shard_name_value).good(), snapshot_exception,
               "Binary snapshot shard name is invalid");

   for (size_t i = 0; i < section_count; i++)
   {
      validate_section();
   }

   uint64_t actual_shard_size = snapshot.tellg() - shard_pos;
   EOS_ASSERT(actual_shard_size == shard_size, snapshot_exception,
               "Binary snapshot shard size invalid.  Expected : ${expected}, Got: ${actual}",
               ("expected", shard_size)("actual", actual_shard_size));
}

void istream_snapshot_reader::validate_section() const {
   uint64_t section_size = 0;
   snapshot.read((char*)&section_size,sizeof(section_size));

   // seek past the section
   snapshot.seekg(snapshot.tellg() + std::streamoff(section_size));
}

void istream_snapshot_reader::init_shards() {

   if (header_pos != snapshot.tellg()) {
      snapshot.seekg(header_pos);
   }

   uint64_t shard_count = 0;
   snapshot.read((char*)&shard_count, sizeof(shard_count));

   for (size_t i = 0; i < shard_count; i++) {
      istream_snapshot_shard_reader::shard_info si;
      si.shard_pos = snapshot.tellg();
      snapshot.read((char*)&si.shard_size, sizeof(si.shard_size));
      snapshot.read((char*)&si.section_count, sizeof(si.section_count));

      uint64_t shard_name_value = 0;
      snapshot.read((char*)&shard_name_value, sizeof(shard_name_value));
      si.shard_name = name(shard_name_value);
      shards[si.shard_name] = si;
   }
}

snapshot_shard_reader_ptr istream_snapshot_reader::read_shard_start( const chain::shard_name& shard_name ) {
   if (is_shards_init) {
      init_shards();
      is_shards_init = true;
   }

   auto itr = shards.find(shard_name);
   EOS_ASSERT(itr != shards.end(), snapshot_exception,
               "Binary snapshot shards has no shard named ${n}.", ("n", shard_name.to_string()));
   cur_shard = std::make_shared<istream_snapshot_shard_reader>(shard_name, snapshot, itr->second);
   return cur_shard;
}

void istream_snapshot_reader::read_shard_end() {
   cur_shard.reset();
   return_to_header();
}

void istream_snapshot_reader::return_to_header() {
   snapshot.seekg( header_pos );
}

struct istream_json_snapshot_shard_reader_impl {
   // eosio_rapidjson::Document& doc;
   eosio_rapidjson::Document::ValueType& shard;
   eosio_rapidjson::Document::ValueType& sections;
   eosio_rapidjson::Document::ValueType* cur_section_rows = nullptr;
   uint64_t num_rows;
   uint64_t cur_row;
   std::string section_name;

   istream_json_snapshot_shard_reader_impl(eosio_rapidjson::Document::ValueType& shard,
                                           eosio_rapidjson::Document::ValueType& sections)
   : shard(shard)
   , sections(sections)
   {}
};

istream_json_snapshot_shard_reader::~istream_json_snapshot_shard_reader() = default;

istream_json_snapshot_shard_reader::istream_json_snapshot_shard_reader(const chain::shard_name& shard_name, impl_type_ptr impl)
: snapshot_shard_reader(shard_name)
, impl(impl)
{
}

void istream_json_snapshot_shard_reader::set_section( const string& section_name ) {
   auto& sections = impl->sections;
   EOS_ASSERT( sections.HasMember( section_name.c_str() ), snapshot_exception, "JSON snapshot shard has no section ${sec}", ("sec", section_name) );
   auto& section = sections[section_name.c_str()];
   EOS_ASSERT( section.HasMember( "num_rows" ), snapshot_exception, "JSON snapshot ${sec} num_rows not found", ("sec", section_name) );
   EOS_ASSERT( section.HasMember( "rows" ), snapshot_exception, "JSON snapshot ${sec} rows not found", ("sec", section_name) );
   EOS_ASSERT( section["rows"].IsArray(), snapshot_exception, "JSON snapshot ${sec} rows is not an array", ("sec_name", section_name) );

   impl->section_name = section_name;
   impl->cur_section_rows = &section["rows"];
   impl->num_rows = section["num_rows"].GetInt();
   EOS_ASSERT( impl->cur_section_rows->Size() == section["num_rows"].GetInt(), snapshot_exception, "JSON snapshot ${sec} rows size not equal to num_rows", ("sec_name", section_name) );

   ilog( "reading ${section_name}, num_rows: ${num_rows}", ("section_name", section_name)( "num_rows", impl->num_rows ) );
}

bool istream_json_snapshot_shard_reader::read_row( detail::abstract_snapshot_row_reader& row_reader ) {
   EOS_ASSERT( impl->cur_section_rows != nullptr, snapshot_exception, "JSON snapshot shard ${shard_name}'s current section not set yet",
               ("shard_name", shard_name.to_string())
               ( "cur_row", impl->cur_row )( "num_rows", impl->num_rows ) );
   EOS_ASSERT( impl->cur_row < impl->num_rows, snapshot_exception, "JSON snapshot shard ${shard_name} section ${sect}'s cur_row ${cur_row} >= num_rows ${num_rows}",
               ("shard_name", shard_name.to_string())
               ("sect_name", impl->section_name)
               ( "cur_row", impl->cur_row )
               ( "num_rows", impl->num_rows ) );

   const eosio_rapidjson::Value& rows = *impl->cur_section_rows;
   eosio_rapidjson::StringBuffer buffer;
   eosio_rapidjson::Writer<eosio_rapidjson::StringBuffer> writer( buffer );
   rows[impl->cur_row].Accept( writer );

   const auto& row = fc::json::from_string( buffer.GetString() );
   row_reader.provide( row );
   return ++impl->cur_row < impl->num_rows;
}

bool istream_json_snapshot_shard_reader::empty() {
   return impl->cur_section_rows == nullptr ||
          impl->num_rows == 0 ||
          impl->cur_row >= impl->num_rows;
}

void istream_json_snapshot_shard_reader::clear_section() {
   impl->cur_section_rows = nullptr;
   impl->num_rows = 0;
   impl->cur_row = 0;
   impl->section_name = "";
}

struct istream_json_snapshot_reader_impl {
   uint64_t num_rows;
   uint64_t cur_row;
   eosio_rapidjson::Document doc;
   std::string sec_name;
};

istream_json_snapshot_reader::~istream_json_snapshot_reader() = default;

istream_json_snapshot_reader::istream_json_snapshot_reader(const fc::path& p)
   : impl{new istream_json_snapshot_reader_impl{0, 0, {}, {}}}
{
   FILE* fp = fopen(p.string().c_str(), "rb");
   EOS_ASSERT(fp, snapshot_exception, "Failed to open JSON snapshot: ${file}", ("file", p));
   auto close = fc::make_scoped_exit( [&fp]() { fclose( fp ); } );
   char readBuffer[65536];
   eosio_rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
   impl->doc.ParseStream(is);
}

void istream_json_snapshot_reader::validate() const {
   try {
      // validate totem
      auto expected_totem = ostream_json_snapshot_writer::magic_number;
      EOS_ASSERT(impl->doc.HasMember("magic_number"), snapshot_exception, "magic_number section not found" );
      auto actual_totem = impl->doc["magic_number"].GetUint();
      EOS_ASSERT( actual_totem == expected_totem, snapshot_exception, "JSON snapshot has unexpected magic number" );

      // validate version
      auto expected_version = current_snapshot_version;
      EOS_ASSERT(impl->doc.HasMember("version"), snapshot_exception, "version section not found" );
      auto actual_version = impl->doc["version"].GetUint();
      EOS_ASSERT( actual_version == expected_version, snapshot_exception,
                  "JSON snapshot is an unsupported version.  Expected : ${expected}, Got: ${actual}",
                  ("expected", expected_version)( "actual", actual_version ) );

      EOS_ASSERT(impl->doc.HasMember("shards"), snapshot_exception, "JSON snapshot shards not found" );
      EOS_ASSERT(impl->doc["shards"].IsObject(), snapshot_exception, "JSON snapshot shards is not an object" );
   } catch( const std::exception& e ) {  \
      snapshot_exception fce(FC_LOG_MESSAGE( warn, "JSON snapshot validation threw IO exception (${what})",("what",e.what())));
      throw fce;
   }
}

bool istream_json_snapshot_reader::validate_section() const {
   return true;
}

snapshot_shard_reader_ptr istream_json_snapshot_reader::read_shard_start( const chain::shard_name& shard_name ) {
   auto& shards = impl->doc["shards"];
   auto shard_name_str = shard_name.to_string();
   EOS_ASSERT(shards.HasMember(shard_name_str.c_str()), snapshot_exception, "JSON snapshot shards has no shard ${s}", ("s", shard_name_str) );
   auto& shard = shards[shard_name_str.c_str()];
   EOS_ASSERT(shard.HasMember("sections"), snapshot_exception, "JSON snapshot shard has no sections");
   auto& sections = shard["sections"];

   auto shard_impl = std::make_shared<istream_json_snapshot_shard_reader_impl>(shard, sections);
   cur_shard = std::make_shared<istream_json_snapshot_shard_reader>(shard_name, shard_impl);

   ilog( "reading shard ${shard_name}, num_sections: ${num_sections}", ("shard_name", shard_name)( "num_sections", sections.Size() ) );
   return cur_shard;
}

void istream_json_snapshot_reader::read_shard_end() {
   cur_shard.reset();
}

void istream_json_snapshot_reader::return_to_header() {
   cur_shard.reset();
}

integrity_hash_snapshot_shard_writer::integrity_hash_snapshot_shard_writer(const chain::shard_name& shard_name, fc::sha256::encoder&  enc)
:snapshot_shard_writer(shard_name), enc(enc)
{
}

void integrity_hash_snapshot_shard_writer::write_start_section( const std::string& )
{
   // no-op for structural details
}

void integrity_hash_snapshot_shard_writer::write_row( const detail::abstract_snapshot_row_writer& row_writer ) {
   row_writer.write(enc);
}

void integrity_hash_snapshot_shard_writer::write_end_section( ) {
   // no-op for structural details
}

void integrity_hash_snapshot_shard_writer::finalize() {
   // no-op for structural details
}


integrity_hash_snapshot_writer::integrity_hash_snapshot_writer(fc::sha256::encoder& enc)
:enc(enc)
{
}

snapshot_shard_writer_ptr integrity_hash_snapshot_writer::add_shard_start( const chain::shard_name& shard_name ) {
   cur_shard = std::make_shared<integrity_hash_snapshot_shard_writer>(shard_name, enc);
   return cur_shard;
}

void integrity_hash_snapshot_writer::add_shard_end(const chain::shard_name& shard_name) {
   cur_shard.reset();
}

void integrity_hash_snapshot_writer::finalize() {
   // no-op for structural details
}

}}
