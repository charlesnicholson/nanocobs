#include "../cobs.h"
#include "doctest.h"

#include <cstring>

TEST_CASE("Decoding validation") {
  unsigned char enc[32], dec[32];
  unsigned dec_len;

  SUBCASE("Invalid payload") {
    // first byte jumps past end
    enc[0] = 3;
    enc[1] = 0;
    REQUIRE(cobs_decode(&enc, 2, dec, sizeof(enc), &dec_len) == COBS_RET_ERR_BAD_PAYLOAD);
  }
}
