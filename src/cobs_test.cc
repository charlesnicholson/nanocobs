#include "cobs.h"
#include "catch.hpp"

TEST_CASE("Parameter validation", "[cobs_encode]") {
  size_t enc_len;
  char buf[16];

  SECTION("Null pointers") {
    REQUIRE( cobs_encode(buf, sizeof(buf), nullptr) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(nullptr, 123, &enc_len) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Bad enc_len values") {
    REQUIRE( cobs_encode(buf, 0, &enc_len) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(buf, 1, &enc_len) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid sentinel values") {
    buf[0] = COBS_ENCODE_SENTINEL_VALUE - 1;
    buf[1] = COBS_ENCODE_SENTINEL_VALUE - 1;
    REQUIRE( cobs_encode(buf, 2, &enc_len) == COBS_RET_ERR_BAD_SENTINELS );

    buf[0] = COBS_ENCODE_SENTINEL_VALUE;
    buf[1] = COBS_ENCODE_SENTINEL_VALUE - 1;
    REQUIRE( cobs_encode(buf, 2, &enc_len) == COBS_RET_ERR_BAD_SENTINELS );

    buf[0] = COBS_ENCODE_SENTINEL_VALUE - 1;
    buf[1] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 2, &enc_len) == COBS_RET_ERR_BAD_SENTINELS );
  }
}

TEST_CASE("Encoding", "[cobs_encode]") {
  unsigned char buf[256];
  size_t enc_len;
  buf[0] = COBS_ENCODE_SENTINEL_VALUE;

  SECTION("Empty") {
    buf[1] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 2, &enc_len) == COBS_RET_SUCCESS );
    REQUIRE( buf[0] == 0x01 );
    REQUIRE( buf[1] == 0x00 );
  }

  SECTION("One nonzero byte") {
    buf[1] = 0x01;
    buf[2] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 3, &enc_len) == COBS_RET_SUCCESS );
    REQUIRE( buf[0] == 0x02 );
    REQUIRE( buf[1] == 0x01 );
    REQUIRE( buf[2] == 0x00 );
  }

  SECTION("One zero byte") {
    buf[1] = 0x00;
    buf[2] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 3, &enc_len) == COBS_RET_SUCCESS );
    REQUIRE( buf[0] == 0x01 );
    REQUIRE( buf[1] == 0x01 );
    REQUIRE( buf[2] == 0x00 );
  }
}

// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples
TEST_CASE("Wikipedia encoding examples", "[cobs_encode]") {
  unsigned char buf[256];
  size_t enc_len;
  buf[0] = COBS_ENCODE_SENTINEL_VALUE;

  SECTION("Example 1") {
    buf[1] = 0x00;
    buf[2] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 3, &enc_len) == COBS_RET_SUCCESS );
    REQUIRE( buf[0] == 0x01 );
    REQUIRE( buf[1] == 0x00 );
  }

  SECTION("Example 2") {
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = COBS_ENCODE_SENTINEL_VALUE;
    REQUIRE( cobs_encode(buf, 4, &enc_len) == COBS_RET_SUCCESS );
    REQUIRE( buf[0] == 0x01 );
    REQUIRE( buf[1] == 0x01 );
    REQUIRE( buf[2] == 0x01 );
    REQUIRE( buf[3] == 0x00 );
  }
}
