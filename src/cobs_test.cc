#include "cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;
static constexpr auto CESV = COBS_ENCODE_SENTINEL_VALUE;

TEST_CASE("Parameter validation", "[cobs_encode]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_encode(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid sentinel values") {
    byte_vec_t buf{CESV - 1, CESV - 1};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_SENTINELS );
    buf = byte_vec_t{CESV, CESV - 1};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_SENTINELS );
    buf = byte_vec_t{CESV - 1, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_SENTINELS );
  }
}

TEST_CASE("Encoding", "[cobs_encode]") {
  SECTION("Empty") {
    byte_vec_t buf{CESV, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x01, 0x00 } );
  }

  SECTION("One nonzero byte") {
    byte_vec_t buf{CESV, 0x01, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x02, 0x01, 0x00 } );
  }

  SECTION("One zero byte") {
    byte_vec_t buf{CESV, 0x00, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x01, 0x01, 0x00 } );
  }
}

// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples
TEST_CASE("Wikipedia encoding examples", "[cobs_encode]") {
  SECTION("Example 1") {
    byte_vec_t buf{CESV, 0x00, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x01, 0x00 } );
  }

  SECTION("Example 2") {
    byte_vec_t buf{CESV, 0x00, 0x00, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x01, 0x01, 0x01, 0x00 } );
  }

  SECTION("Example 3") {
    byte_vec_t buf{CESV, 0x11, 0x00, 0x22, 0x33, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 } );
  }

  SECTION("Example 4") {
    byte_vec_t buf{CESV, 0x11, 0x22, 0x33, 0x44, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 } );
  }

  SECTION("Example 5") {
    byte_vec_t buf{CESV, 0x11, 0x00, 0x00, 0x00, CESV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{ 0x02, 0x11, 0x01, 0x01, 0x01, 0x00 } );
  }
}
