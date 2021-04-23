#include "../cobs.h"
#include "catch.hpp"

TEST_CASE("cobs_encode_max", "[cobs_encode_max]") {
  SECTION("0 bytes") { REQUIRE( cobs_encode_max(0) == 2 ); }
  SECTION("1 byte")  { REQUIRE( cobs_encode_max(1) == 3 ); }
  SECTION("2 bytes") { REQUIRE( cobs_encode_max(2) == 4 ); }

  SECTION("3 - 254 bytes") {
    for (auto i = 3u; i <= 254u; ++i) {
      REQUIRE( cobs_encode_max(i) == i + 2 );
    }
  }

  SECTION("255-508 bytes") {
    for (auto i = 255u; i <= 508u; ++i) { REQUIRE( cobs_encode_max(i) == i + 3 ); }
  }

  SECTION("Input size plus boilerplate plus ceil(x/254)") {
    REQUIRE( cobs_encode_max(12345) == 1 + 12345 + ((12345 + 253) / 254) );
  }
}
