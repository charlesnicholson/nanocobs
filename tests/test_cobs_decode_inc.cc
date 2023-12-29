#include "../cobs.h"
#include "byte_vec.h"
#include "doctest.h"

#include <cstring>

TEST_CASE("cobs_decode_inc_begin") {
  REQUIRE(cobs_decode_inc_begin(nullptr) == COBS_RET_ERR_BAD_ARG);
  cobs_decode_inc_ctx_t c;
  memset(&c, 0xFF, sizeof(c));
  REQUIRE(cobs_decode_inc_begin(&c) == COBS_RET_SUCCESS);
  REQUIRE(c.state == cobs_decode_inc_ctx_t::COBS_DECODE_READ_CODE);
}

TEST_CASE("cobs_decode_inc") {
}
