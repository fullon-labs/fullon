#include <eosio/chain/block_timestamp.hpp>

#include <boost/test/unit_test.hpp>

#include <fc/time.hpp>
#include <fc/exception/exception.hpp>

using namespace eosio;
using namespace chain;

BOOST_AUTO_TEST_SUITE(block_timestamp_tests)

static uint32_t sec_to_slot(uint32_t sec) {
	uint64_t ret = uint64_t(sec) * 1000 / config::block_interval_ms;
	return ret;
}
static uint32_t slot_to_second(uint32_t slot) {
	uint64_t ret = (uint64_t)slot * config::block_interval_ms / 1000;
	return ret;
}

BOOST_AUTO_TEST_CASE(constructor_test) {
	block_timestamp_type bt;
        BOOST_TEST( bt.slot == 0u, "Default constructor gives wrong value");

	fc::time_point t(fc::seconds(978307200));
	block_timestamp_type bt2(t);
	BOOST_REQUIRE_EQUAL( bt2.slot, sec_to_slot(978307200u - 946684800u));
}

BOOST_AUTO_TEST_CASE(conversion_test) {
	block_timestamp_type bt;
	fc::time_point t = (fc::time_point)bt;
	BOOST_TEST(t.time_since_epoch().to_seconds() == 946684800ll, "Time point conversion failed");

	block_timestamp_type bt1(200);
	t = (fc::time_point)bt1;
	BOOST_TEST(t.time_since_epoch().to_seconds() == 946684800ll + slot_to_second(200), "Time point conversion failed");

}

BOOST_AUTO_TEST_SUITE_END()
