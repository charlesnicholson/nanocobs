#include "../cobs.h"
#include "catch.hpp"

using byte_vec_t = std::vector< unsigned char >;
static constexpr unsigned char CSV = COBS_SENTINEL_VALUE;

TEST_CASE("Encode parameter validation", "[cobs_encode]") {
  SECTION("Null buffer pointer") {
    REQUIRE( cobs_encode(nullptr, 123) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid buf_len") {
    char buf;
    REQUIRE( cobs_encode(&buf, 0) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(&buf, 1) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(&buf, 258) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_encode(&buf, 1024) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid sentinel values") {
    byte_vec_t buf{CSV - 1, CSV - 1};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV, CSV - 1};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{CSV - 1, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}

TEST_CASE("Encoding", "[cobs_encode]") {
  SECTION("Empty") {
    byte_vec_t buf{CSV, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x00} );
  }

  SECTION("One nonzero byte") {
    byte_vec_t buf{CSV, 0x01, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x02, 0x01, 0x00} );
  }

  SECTION("One zero byte") {
    byte_vec_t buf{CSV, 0x00, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x01, 0x00} );
  }
}

// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples
TEST_CASE("Wikipedia encoding examples", "[cobs_encode]") {
  SECTION("Example 1") {
    byte_vec_t buf{CSV, 0x00, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x01, 0x00} );
  }

  SECTION("Example 2") {
    byte_vec_t buf{CSV, 0x00, 0x00, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x01, 0x01, 0x01, 0x00} );
  }

  SECTION("Example 3") {
    byte_vec_t buf{CSV, 0x11, 0x22, 0x00, 0x33, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x03, 0x11, 0x22, 0x02, 0x33, 0x00} );
  }

  SECTION("Example 4") {
    byte_vec_t buf{CSV, 0x11, 0x22, 0x33, 0x44, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x05, 0x11, 0x22, 0x33, 0x44, 0x00} );
  }

  SECTION("Example 5") {
    byte_vec_t buf{CSV, 0x11, 0x00, 0x00, 0x00, CSV};
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{0x02, 0x11, 0x01, 0x01, 0x01, 0x00} );
  }

  SECTION("Example 6") {
    byte_vec_t buf{CSV};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      buf.push_back(i);
    }
    buf.push_back(CSV);
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );

    byte_vec_t expected{0xFF};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      expected.push_back(i);
    }
    expected.push_back(0x00);
    REQUIRE( buf == expected );
  }

  SECTION("Example 7") {
    byte_vec_t buf{CSV};
    for (unsigned char i = 0x00u; i <= 0xFE; ++i) {
      buf.push_back(i);
    }
    buf.push_back(CSV);
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );

    byte_vec_t expected{0x01, 0xFF};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      expected.push_back(i);
    }
    expected.push_back(0x00);
    REQUIRE( buf == expected );
  }

  // Examples 8 and 9 can't be done in-place, need two calls.

  SECTION("Example 10") {
    byte_vec_t buf{CSV};
    for (auto i = 0x03u; i <= 0xFF; ++i) {
      buf.push_back(static_cast< unsigned char >(i));
    }
    buf.insert(buf.end(), {0x00, 0x01, CSV});
    REQUIRE( cobs_encode(buf.data(), buf.size()) == COBS_RET_SUCCESS );

    byte_vec_t expected{0xFE};
    for (auto i = 0x03u; i <= 0xFF; ++i) {
      expected.push_back(static_cast< unsigned char >(i));
    }
    expected.insert(expected.end(), {0x02, 0x01, 0x00});
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
    REQUIRE( cobs_decode(&buf, 258) == COBS_RET_ERR_BAD_ARG );
    REQUIRE( cobs_decode(&buf, 1024) == COBS_RET_ERR_BAD_ARG );
  }

  SECTION("Invalid payload") {
    byte_vec_t buf{0x00, 0x00}; // can't start with 0x00
    REQUIRE( cobs_decode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_PAYLOAD );
    buf = byte_vec_t{0x01, 0x01}; // must end with 0x00
    REQUIRE( cobs_decode(buf.data(), buf.size()) == COBS_RET_ERR_BAD_PAYLOAD );
  }
}

TEST_CASE("Decoding", "[cobs_decode]") {
  SECTION("Empty") {
    byte_vec_t buf{0x01, 0x00};
    REQUIRE( cobs_decode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, CSV} );
  }

  SECTION("One nonzero byte") {
    byte_vec_t buf{0x02, 0x01, 0x00};
    REQUIRE( cobs_decode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, 0x01, CSV} );
  }

  SECTION("One zero byte") {
    byte_vec_t buf{0x01, 0x01, 0x00};
    REQUIRE( cobs_decode(buf.data(), buf.size()) == COBS_RET_SUCCESS );
    REQUIRE( buf == byte_vec_t{CSV, 0x00, CSV} );
  }
}

