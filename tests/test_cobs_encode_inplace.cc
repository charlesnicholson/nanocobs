#include "../cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;
static constexpr unsigned char CSV = COBS_INPLACE_SENTINEL_VALUE;

namespace {
  cobs_ret_t cobs_encode_vec(byte_vec_t &v) {
    return cobs_encode_inplace(v.data(), static_cast< unsigned >(v.size()));
  }
}

TEST_CASE("Inplace encoding validation", "[cobs_encode_inplace]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_encode_inplace(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid buf_len") {
    char buf;
    REQUIRE( cobs_encode_inplace(&buf, 0) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode_inplace(&buf, 1) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid sentinel values") {
    byte_vec_t buf{CSV - 1, CSV - 1};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV, CSV - 1};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV - 1, CSV};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }

  SECTION("Nonzero run longer than 255") {
    byte_vec_t buf{CSV};
    buf.insert(std::end(buf), 256, 0x01);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }

  SECTION("Non-final run of 255 bytes") {
    byte_vec_t buf{CSV, 0x00};
    buf.insert(std::end(buf), 255, 1);
    buf.push_back(0x00);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}

TEST_CASE("Inplace encoding", "[cobs_encode_inplace]") {
  SECTION("Empty") {
    byte_vec_t buf{CSV, CSV};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x00} );
  }

  SECTION("One nonzero byte") {
    byte_vec_t buf{CSV, 0x01, CSV};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x02, 0x01, 0x00} );
  }

  SECTION("One zero byte") {
    byte_vec_t buf{CSV, 0x00, CSV};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x01, 0x00} );
  }

  /*
  SECTION("Final run of 255 bytes") {
    byte_vec_t buf{CSV, 0x00};
    buf.insert(std::end(buf), 255, 1);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );
  }
  */

  SECTION("Safe payload, all zero bytes") {
    byte_vec_t buf(COBS_INPLACE_SAFE_BUFFER_SIZE);
    std::fill(std::begin(buf), std::end(buf), 0x00);
    buf[0] = CSV;
    buf[buf.size() - 1] = CSV;
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected(buf.size());
    std::fill(std::begin(expected), std::end(expected), 0x01);
    expected[expected.size() - 1] = 0x00;
    REQUIRE( buf == expected );
  }

  SECTION("Safe payload, no zero bytes") {
    byte_vec_t buf(COBS_INPLACE_SAFE_BUFFER_SIZE);
    std::iota(std::begin(buf), std::end(buf), 0x00);
    buf[0] = CSV;
    buf[buf.size() - 1] = CSV;
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected(buf.size());
    std::iota(std::begin(expected), std::end(expected), 0x00);
    expected[0] = 0xFF;
    expected[expected.size() - 1] = 0x00;
    REQUIRE( buf == expected );
  }

  SECTION("Unsafe payload with 254B jumps") {
    byte_vec_t buf{CSV};
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0x00);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0x00);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_SUCCESS );

    byte_vec_t expected{0xFF};
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0x00);
    REQUIRE( buf == expected );
  }
}

namespace {
void verify_encode_inplace(unsigned char *inplace, unsigned payload_len) {
  byte_vec_t external(cobs_encode_max(payload_len));
  unsigned external_len;
  REQUIRE( cobs_encode(inplace + 1,
                       payload_len,
                       external.data(),
                       static_cast< unsigned >(external.size()),
                       &external_len) == COBS_RET_SUCCESS );

  REQUIRE( cobs_encode_inplace(inplace, payload_len + 2) == COBS_RET_SUCCESS );
  REQUIRE( byte_vec_t(inplace, inplace + payload_len + 2) == external );
}

void fill_inplace(unsigned char *inplace, unsigned payload_len, unsigned char f) {
  memset(inplace + 1, f, payload_len);
  inplace[0] = COBS_INPLACE_SENTINEL_VALUE;
  inplace[payload_len+1] = COBS_INPLACE_SENTINEL_VALUE;
}
}

TEST_CASE("Encode: Inplace == External", "[cobs_encode_inplace]") {
  unsigned char inplace[COBS_INPLACE_SAFE_BUFFER_SIZE];

  SECTION("Fill with zeros") {
    for (auto i = 0u; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0x00);
      verify_encode_inplace(inplace, i);
    }
  }

  SECTION("Fill with nonzeros") {
    for (auto i = 0u; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0x01);
      verify_encode_inplace(inplace, i);
    }
  }

  SECTION("Fill with 0xFF") {
    for (auto i = 0u; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0xFF);
      verify_encode_inplace(inplace, i);
    }
  }

  SECTION("Fill with zero/one pattern") {
    for (auto i = 0u; i < sizeof(inplace) - 2; ++i) {
      inplace[0] = COBS_INPLACE_SENTINEL_VALUE;
      for (auto j = 1u; j < i; ++j) {
        inplace[j] = j & 1;
      }
      inplace[i + 1] = COBS_INPLACE_SENTINEL_VALUE;
      verify_encode_inplace(inplace, i);
    }
  }

  SECTION("Fill with one/zero pattern") {
    for (auto i = 0u; i < sizeof(inplace) - 2; ++i) {
      inplace[0] = COBS_INPLACE_SENTINEL_VALUE;
      for (auto j = 1u; j < i; ++j) {
        inplace[j] = (j & 1) ^ 1;
      }
      inplace[i + 1] = COBS_INPLACE_SENTINEL_VALUE;
      verify_encode_inplace(inplace, i);
    }
  }
}
