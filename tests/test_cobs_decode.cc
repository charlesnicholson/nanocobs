#include "../cobs.h"
#include "catch.hpp"

#include <cstring>

TEST_CASE("Decoding validation", "[cobs_decode]") {
  unsigned char enc[32], dec[32];
  unsigned dec_len;

  SECTION("Invalid payload") {
    // first byte jumps past end
    enc[0] = 3;
    enc[1] = 0;
    REQUIRE( cobs_decode(&enc, 2, dec, sizeof(enc), &dec_len) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}
