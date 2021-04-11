#include "../cobs.h"
#include "catch.hpp"

#include <numeric>

using byte_vec_t = std::vector< unsigned char >;
static constexpr unsigned char CSV = COBS_SENTINEL_VALUE;

namespace {
  cobs_ret_t cobs_encode_vec(byte_vec_t &v) {
    return cobs_encode(v.data(), static_cast< unsigned >(v.size()));
  }

  cobs_ret_t cobs_decode_vec(byte_vec_t &v) {
    return cobs_decode(v.data(), static_cast< unsigned >(v.size()));
  }
}

TEST_CASE("Encode parameter validation", "[cobs_encode]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_encode(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid buf_len") {
    char buf;
    REQUIRE( cobs_encode(&buf, 0) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(&buf, 1) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid sentinel values") {
    byte_vec_t buf{CSV - 1, CSV - 1};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV, CSV - 1};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV - 1, CSV};
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }

  SECTION("Invalid payload: last jump == 256B") {
    byte_vec_t buf{CSV, 0x00};
    buf.insert(std::end(buf), 255, 1);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }

  SECTION("Invalid payload: non-last jump == 256B") {
    byte_vec_t buf{CSV, 0x00};
    buf.insert(std::end(buf), 255, 1);
    buf.push_back(CSV);
    REQUIRE( cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}

TEST_CASE("Encoding", "[cobs_encode]") {
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

  SECTION("Safe payload, all zero bytes") {
    byte_vec_t buf(COBS_SAFE_BUFFER_SIZE);
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
    byte_vec_t buf(COBS_SAFE_BUFFER_SIZE);
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

TEST_CASE("Decode parameter validation", "[cobs_decode]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_decode(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid buf_len") {
    char buf;
    REQUIRE( cobs_decode(&buf, 0) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_decode(&buf, 1) == COBS_RET_ERR_BAD_ARG );
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

TEST_CASE("Decoding", "[cobs_decode]") {
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

namespace {
  void round_trip(byte_vec_t const &decoded, byte_vec_t const &encoded) {
    byte_vec_t x(decoded);
    REQUIRE( cobs_encode_vec(x) == COBS_RET_SUCCESS );
    REQUIRE( x == encoded );
    REQUIRE( cobs_decode_vec(x) == COBS_RET_SUCCESS );
    REQUIRE( x == decoded );
  }
}
// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples
TEST_CASE("Wikipedia round-trip examples", "[cobs_encode][cobs_decode]") {
  SECTION("Example 1") {
    round_trip({CSV, 0x00, CSV}, {0x01, 0x01, 0x00});
  }

  SECTION("Example 2") {
    round_trip({CSV, 0x00, 0x00, CSV}, {0x01, 0x01, 0x01, 0x00});
  }

  SECTION("Example 3") {
    round_trip({CSV,  0x11, 0x22, 0x00, 0x33, CSV},
               {0x03, 0x11, 0x22, 0x02, 0x33, 0x00});
  }

  SECTION("Example 4") {
    round_trip({CSV,  0x11, 0x22, 0x33, 0x44, CSV},
               {0x05, 0x11, 0x22, 0x33, 0x44, 0x00});
  }

  SECTION("Example 5") {
    round_trip({CSV,  0x11, 0x00, 0x00, 0x00, CSV},
               {0x02, 0x11, 0x01, 0x01, 0x01, 0x00});
  }

  SECTION("Example 6") {
    byte_vec_t decoded{CSV};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      decoded.push_back(i);
    }
    decoded.push_back(CSV);

    byte_vec_t encoded{0xFF};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      encoded.push_back(i);
    }
    encoded.push_back(0x00);

    round_trip(decoded, encoded);
  }

  SECTION("Example 7") {
    byte_vec_t decoded{CSV};
    for (unsigned char i = 0x00u; i <= 0xFE; ++i) {
      decoded.push_back(i);
    }
    decoded.push_back(CSV);

    byte_vec_t encoded{0x01, 0xFF};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      encoded.push_back(i);
    }
    encoded.push_back(0x00);

    round_trip(decoded, encoded);
  }

  // Examples 8 and 9 can't be done in-place, need two calls.

  SECTION("Example 10") {
    byte_vec_t decoded{CSV};
    for (auto i = 0x03u; i <= 0xFF; ++i) {
      decoded.push_back(static_cast< unsigned char >(i));
    }
    decoded.insert(decoded.end(), {0x00, 0x01, CSV});

    byte_vec_t encoded{0xFE};
    for (auto i = 0x03u; i <= 0xFF; ++i) {
      encoded.push_back(static_cast< unsigned char >(i));
    }
    encoded.insert(encoded.end(), {0x02, 0x01, 0x00});

    round_trip(decoded, encoded);
  }
}

