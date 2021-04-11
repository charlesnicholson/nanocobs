#include "../cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;

TEST_CASE("Encoding validation", "[cobs_encode]") {
  unsigned char enc[32], dec[32];
  unsigned const enc_n = sizeof(enc);
  unsigned const dec_n = sizeof(dec);
  unsigned enc_len;

  SECTION("Null buffer pointer") {
    REQUIRE( cobs_encode(nullptr, dec_n, enc_n, enc, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );

    REQUIRE( cobs_encode(dec, dec_n, enc_n, nullptr, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );

    REQUIRE( cobs_encode(dec, dec_n, enc_n, enc, nullptr) ==
             COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid enc_max") {
    REQUIRE( cobs_encode(dec, dec_n, 0, enc, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );

    REQUIRE( cobs_encode(dec, dec_n, 1, enc, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );

    REQUIRE( cobs_encode(dec, dec_n, dec_n - 2, enc, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );

    REQUIRE( cobs_encode(dec, dec_n, dec_n - 1, enc, &enc_len) ==
             COBS_RET_ERR_BAD_ARG );
  }
}
