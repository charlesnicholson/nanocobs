#include "../cobs.h"
#include "cobs_encode_max_c.h"
#include "doctest_wrapper.h"

enum { WORKS_AT_COMPILE_TIME = COBS_ENCODE_MAX(123) };

TEST_CASE("COBS_ENCODE_MAX") {
  SUBCASE("0 bytes") {
    REQUIRE(COBS_ENCODE_MAX(0) == 2);
    REQUIRE(cobs_encode_max_c(0) == 2);
  }
  SUBCASE("1 byte") {
    REQUIRE(COBS_ENCODE_MAX(1) == 3);
    REQUIRE(cobs_encode_max_c(1) == 3);
  }
  SUBCASE("2 bytes") {
    REQUIRE(COBS_ENCODE_MAX(2) == 4);
    REQUIRE(cobs_encode_max_c(2) == 4);
  }

  SUBCASE("3 - 254 bytes (overhead = 2)") {
    for (auto i{ 3u }; i <= 254u; ++i) {
      REQUIRE(COBS_ENCODE_MAX(i) == i + 2);
      REQUIRE(cobs_encode_max_c(i) == i + 2);
    }
  }

  SUBCASE("255-508 bytes (overhead = 3)") {
    for (auto i{ 255u }; i <= 508u; ++i) {
      REQUIRE(COBS_ENCODE_MAX(i) == i + 3);
      REQUIRE(cobs_encode_max_c(i) == i + 3);
    }
  }

  SUBCASE("509-762 bytes (overhead = 4)") {
    for (auto i{ 509u }; i <= 762u; ++i) {
      REQUIRE(COBS_ENCODE_MAX(i) == i + 4);
      REQUIRE(cobs_encode_max_c(i) == i + 4);
    }
  }

  SUBCASE("Boundary values") {
    REQUIRE(COBS_ENCODE_MAX(254) == 256);
    REQUIRE(COBS_ENCODE_MAX(255) == 258);
    REQUIRE(COBS_ENCODE_MAX(508) == 511);
    REQUIRE(COBS_ENCODE_MAX(509) == 513);
    REQUIRE(COBS_ENCODE_MAX(762) == 766);
    REQUIRE(COBS_ENCODE_MAX(763) == 768);
  }

  SUBCASE("Formula: 1 + n + ceil(n/254) for n > 0") {
    for (size_t n{ 1 }; n <= 2048; ++n) {
      size_t const expected = 1 + n + ((n + 253) / 254);
      REQUIRE(COBS_ENCODE_MAX(n) == expected);
      REQUIRE(cobs_encode_max_c(n) == expected);
    }
  }

  SUBCASE("Monotonically increasing") {
    for (size_t n{ 0 }; n < 2048; ++n) {
      REQUIRE(COBS_ENCODE_MAX(n) < COBS_ENCODE_MAX(n + 1));
    }
  }

  SUBCASE("Always at least n + 2") {
    for (size_t n{ 0 }; n <= 2048; ++n) {
      REQUIRE(COBS_ENCODE_MAX(n) >= n + 2);
    }
  }

  SUBCASE("C and C++ implementations agree across wide range") {
    for (size_t n{ 0 }; n <= 4096; ++n) {
      REQUIRE(COBS_ENCODE_MAX(n) == cobs_encode_max_c(n));
    }
  }

  SUBCASE("Large values") {
    REQUIRE(COBS_ENCODE_MAX(12345) == 1 + 12345 + ((12345 + 253) / 254));
    REQUIRE(cobs_encode_max_c(12345) == 1 + 12345 + ((12345 + 253) / 254));
    REQUIRE(COBS_ENCODE_MAX(65535) == 1 + 65535 + ((65535 + 253) / 254));
    REQUIRE(cobs_encode_max_c(65535) == 1 + 65535 + ((65535 + 253) / 254));
    REQUIRE(COBS_ENCODE_MAX(1000000) == 1 + 1000000 + ((1000000 + 253) / 254));
    REQUIRE(cobs_encode_max_c(1000000) == 1 + 1000000 + ((1000000 + 253) / 254));
  }
}
