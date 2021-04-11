#include "../cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;
static constexpr unsigned char CSV = COBS_INPLACE_SENTINEL_VALUE;

namespace {
  cobs_ret_t cobs_decode_vec(byte_vec_t &v) {
    return cobs_decode_inplace(v.data(), static_cast< unsigned >(v.size()));
  }
}

TEST_CASE("Inplace decoding validation", "[cobs_decode_inplace]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_decode_inplace(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid buf_len") {
    char buf;
    REQUIRE( cobs_decode_inplace(&buf, 0) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_decode_inplace(&buf, 1) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid payload") {
    byte_vec_t buf{0x00, 0x00}; // can't start with 0x00
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{0x01, 0x01}; // must end with 0x00
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{0x01, 0x02, 0x00}; // internal byte jumps past the end
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{0x03, 0x01, 0x00}; // first byte jumps past end
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}

TEST_CASE("Inplace decoding", "[cobs_decode_inplace]") {
  SECTION("Empty") {
    byte_vec_t buf{0x01, 0x00};
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, CSV} );
  }

  SECTION("One nonzero byte") {
    byte_vec_t buf{0x02, 0x01, 0x00};
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, 0x01, CSV} );
  }

  SECTION("One zero byte") {
    byte_vec_t buf{0x01, 0x01, 0x00};
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, 0x00, CSV} );
  }
}
