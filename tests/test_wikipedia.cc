#include "../cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;
static constexpr unsigned char CSV = COBS_INPLACE_SENTINEL_VALUE;

namespace {
  void round_trip_inplace(byte_vec_t const &decoded,
                          byte_vec_t const &encoded) {
    byte_vec_t x(decoded);
    unsigned const n = static_cast< unsigned >(x.size());
    REQUIRE( cobs_encode_inplace(x.data(), n) == COBS_RET_SUCCESS );
    REQUIRE( x == encoded );
    REQUIRE( cobs_decode_inplace(x.data(), n) == COBS_RET_SUCCESS );
    REQUIRE( x == decoded );
  }
}

// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples
TEST_CASE("Wikipedia round-trip examples", "[cobs_encode][cobs_decode]") {
  SECTION("Example 1") {
    round_trip_inplace({CSV, 0x00, CSV}, {0x01, 0x01, 0x00});
  }

  SECTION("Example 2") {
    round_trip_inplace({CSV, 0x00, 0x00, CSV}, {0x01, 0x01, 0x01, 0x00});
  }

  SECTION("Example 3") {
    round_trip_inplace({CSV,  0x11, 0x22, 0x00, 0x33, CSV},
                       {0x03, 0x11, 0x22, 0x02, 0x33, 0x00});
  }

  SECTION("Example 4") {
    round_trip_inplace({CSV,  0x11, 0x22, 0x33, 0x44, CSV},
                       {0x05, 0x11, 0x22, 0x33, 0x44, 0x00});
  }

  SECTION("Example 5") {
    round_trip_inplace({CSV,  0x11, 0x00, 0x00, 0x00, CSV},
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

    round_trip_inplace(decoded, encoded);
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

    round_trip_inplace(decoded, encoded);
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

    round_trip_inplace(decoded, encoded);
  }
}
