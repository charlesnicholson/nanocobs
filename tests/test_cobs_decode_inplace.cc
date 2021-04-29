#include "../cobs.h"
#include "catch.hpp"

#include <numeric>

using byte_t = unsigned char;
using byte_vec_t = std::vector< byte_t >;
static constexpr byte_t CSV = COBS_INPLACE_SENTINEL_VALUE;

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

  SECTION("Safe payload, all zero bytes") {
    byte_vec_t buf(COBS_INPLACE_SAFE_BUFFER_SIZE);
    std::fill(std::begin(buf), std::end(buf), byte_t{0x01});
    buf[buf.size() - 1] = 0x00;
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected(buf.size());
    std::fill(std::begin(expected), std::end(expected), byte_t{0x00});
    expected[0] = CSV;
    expected[expected.size() - 1] = CSV;

    REQUIRE( buf == expected );
  }

  SECTION("Safe payload, no zero bytes") {
    byte_vec_t buf(COBS_INPLACE_SAFE_BUFFER_SIZE);
    std::iota(std::begin(buf), std::end(buf), byte_t{0x00});
    buf[0] = 0xFF;
    buf[buf.size() - 1] = 0x00;
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected(buf.size());
    std::iota(std::begin(expected), std::end(expected), byte_t{0x00});
    expected[0] = CSV;
    expected[expected.size() - 1] = CSV;
    REQUIRE( buf == expected );
  }

  SECTION("Unsafe payload with 254B jumps") {
    byte_vec_t buf{0xFF};
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0xFF);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0xFF);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0x00);
    REQUIRE( cobs_decode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected{CSV};
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0x00);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0x00);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(CSV);
    REQUIRE( expected == buf );
  }
}
