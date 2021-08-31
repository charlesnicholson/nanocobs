#include "../cobs.h"
#include "byte_vec.h"
#include "doctest.h"

#include <numeric>

static constexpr byte_t CSV = COBS_INPLACE_SENTINEL_VALUE;

namespace {
void round_trip_inplace(byte_vec_t const &decoded,
                        byte_vec_t const &encoded) {
  byte_vec_t decoded_inplace{CSV};
  decoded_inplace.insert(
    std::end(decoded_inplace), std::begin(decoded), std::end(decoded));
  decoded_inplace.push_back(CSV);

  byte_vec_t x(decoded_inplace);
  unsigned const n = static_cast< unsigned >(x.size());

  REQUIRE(cobs_encode_inplace(x.data(), n) == COBS_RET_SUCCESS);
  REQUIRE(x == encoded);
  REQUIRE(cobs_decode_inplace(x.data(), n) == COBS_RET_SUCCESS);
  REQUIRE(x == decoded_inplace);
}

void round_trip(byte_vec_t const &decoded, byte_vec_t const &encoded) {
  unsigned char enc_actual[512], dec_actual[512];
  unsigned enc_actual_len, dec_actual_len;

  REQUIRE(cobs_encode(decoded.data(),
                      static_cast< unsigned >(decoded.size()),
                      enc_actual,
                      sizeof(enc_actual),
                      &enc_actual_len) == COBS_RET_SUCCESS);

  REQUIRE(encoded == byte_vec_t(enc_actual, enc_actual + enc_actual_len));

  REQUIRE(cobs_decode(enc_actual,
                      enc_actual_len,
                      dec_actual,
                      sizeof(dec_actual),
                      &dec_actual_len) == COBS_RET_SUCCESS);

  REQUIRE(decoded == byte_vec_t(dec_actual, dec_actual + dec_actual_len));

  // Additionaly, in-place decode atop enc_actual using cobs_decode.

  REQUIRE(cobs_decode(enc_actual,
                      enc_actual_len,
                      enc_actual,
                      sizeof(enc_actual),
                      &dec_actual_len) == COBS_RET_SUCCESS);

  REQUIRE(decoded == byte_vec_t(enc_actual, enc_actual + dec_actual_len));
}
}

// https://wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing#Encoding_examples

TEST_CASE("Wikipedia round-trip examples") {
  SUBCASE("Example 1") {
    const byte_vec_t decoded{0x00};
    const byte_vec_t encoded{0x01, 0x01, 0x00};
    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 2") {
    const byte_vec_t decoded{0x00, 0x00};
    const byte_vec_t encoded{0x01, 0x01, 0x01, 0x00};
    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 3") {
    const byte_vec_t decoded{0x11, 0x22, 0x00, 0x33};
    const byte_vec_t encoded{0x03, 0x11, 0x22, 0x02, 0x33, 0x00};
    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 4") {
    const byte_vec_t decoded{0x11, 0x22, 0x33, 0x44};
    const byte_vec_t encoded{0x05, 0x11, 0x22, 0x33, 0x44, 0x00};
    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 5") {
    const byte_vec_t decoded{0x11, 0x00, 0x00, 0x00};
    const byte_vec_t encoded{0x02, 0x11, 0x01, 0x01, 0x01, 0x00};
    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 6") {
    // 01 02 03 ... FD FE
    byte_vec_t decoded(254);
    std::iota(std::begin(decoded), std::end(decoded), byte_t{0x01});

    // FF 01 02 03 ... FD FE 00
    byte_vec_t encoded(255);
    std::iota(std::begin(encoded), std::end(encoded), byte_t{0x00});
    encoded[0] = 0xFF;
    encoded.push_back(0x00);

    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 7") {
    // 00 01 02 ... FC FD FE
    byte_vec_t decoded(255);
    std::iota(std::begin(decoded), std::end(decoded), byte_t{0x00});

    // 01 FF 01 02 ... FC FD FE 00
    byte_vec_t encoded{0x01, 0xFF};
    for (unsigned char i = 0x01u; i <= 0xFE; ++i) {
      encoded.push_back(i);
    }
    encoded.push_back(0x00);

    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }

  SUBCASE("Example 8") {
    // 01 02 03 ... FD FE FF
    byte_vec_t decoded(255);
    std::iota(std::begin(decoded), std::end(decoded), byte_t{0x01});

    // FF 01 02 03 ... FD FE 02 FF 00
    byte_vec_t encoded(255);
    std::iota(std::begin(encoded), std::end(encoded), byte_t{0x00});
    encoded[0] = 0xFF;
    encoded.insert(std::end(encoded), {0x02, 0xFF, 0x00});

    round_trip(decoded, encoded);
  }

  SUBCASE("Example 9") {
    // 02 03 04 ... FE FF 00
    byte_vec_t decoded(255);
    std::iota(std::begin(decoded), std::end(decoded), byte_t{0x02});
    decoded[decoded.size() - 1] = 0x00;

    // FF 02 03 04 ... FE FF 01 01 00
    byte_vec_t encoded(255);
    std::iota(std::begin(encoded), std::end(encoded), byte_t{0x01});
    encoded[0] = 0xFF;
    encoded.insert(std::end(encoded), {0x01, 0x01, 0x00});

    round_trip(decoded, encoded);
  }

  SUBCASE("Example 10") {
    // 03 04 05 ... FF 00 01
    byte_vec_t decoded(253);
    std::iota(std::begin(decoded), std::end(decoded), byte_t{0x03});
    decoded.insert(decoded.end(), {0x00, 0x01});

    // FE 03 04 05 ... FF 02 01 00
    byte_vec_t encoded{0xFE};
    for (auto i = 0x03u; i <= 0xFF; ++i) {
      encoded.push_back(static_cast< byte_t >(i));
    }
    encoded.insert(encoded.end(), {0x02, 0x01, 0x00});

    round_trip_inplace(decoded, encoded);
    round_trip(decoded, encoded);
  }
}
