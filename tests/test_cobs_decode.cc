#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <cstring>
#include <numeric>

namespace {
byte_vec_t decode(byte_vec_t const& enc) {
  byte_vec_t dec(enc.size());
  size_t dec_len{ 0u };
  REQUIRE(cobs_decode(enc.data(), enc.size(), dec.data(), dec.size(), &dec_len) ==
          COBS_RET_SUCCESS);
  dec.resize(dec_len);
  return dec;
}

byte_vec_t encode(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t enc_len{ 0u };
  REQUIRE(cobs_encode(dec.data(), dec.size(), enc.data(), enc.size(), &enc_len) ==
          COBS_RET_SUCCESS);
  enc.resize(enc_len);
  return enc;
}
}  // namespace

TEST_CASE("Decoding validation") {
  unsigned char dec[32];
  size_t dec_len;

  SUBCASE("Null pointers") {
    byte_vec_t enc{ 0x01, 0x00 };
    REQUIRE(cobs_decode(nullptr, enc.size(), dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode(enc.data(), enc.size(), nullptr, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), nullptr) ==
            COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Invalid enc_len") {
    byte_vec_t enc{ 0x00 };
    REQUIRE(cobs_decode(enc.data(), 0, dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode(enc.data(), 1, dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Invalid payload: code byte jumps past end") {
    byte_vec_t enc{ 3, 0 };
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Invalid payload: code byte jumps over internal zeroes") {
    byte_vec_t enc{ 5, 1, 0, 0, 1, 0 };
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Invalid payload: embedded zero in run") {
    byte_vec_t enc{ 0x04, 0x01, 0x00, 0x03, 0x00 };
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Output buffer too small") {
    byte_vec_t enc{ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 };
    unsigned char tiny[2];
    REQUIRE(cobs_decode(enc.data(), enc.size(), tiny, sizeof(tiny), &dec_len) ==
            COBS_RET_ERR_EXHAUSTED);
  }

  SUBCASE("Output buffer exactly right") {
    byte_vec_t enc{ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 };
    unsigned char exact[4];
    REQUIRE(cobs_decode(enc.data(), enc.size(), exact, sizeof(exact), &dec_len) ==
            COBS_RET_SUCCESS);
    REQUIRE(dec_len == 4);
  }

  SUBCASE("Missing trailing delimiter") {
    byte_vec_t enc{ 0x02, 0x01 };
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), &dec_len) ==
            COBS_RET_ERR_EXHAUSTED);
  }
}

TEST_CASE("Simple decodings") {
  SUBCASE("Empty payload") {
    REQUIRE(decode({ 0x01, 0x00 }) == byte_vec_t{});
  }

  SUBCASE("1 nonzero byte") {
    REQUIRE(decode({ 0x02, 0x34, 0x00 }) == byte_vec_t{ 0x34 });
  }

  SUBCASE("2 nonzero bytes") {
    REQUIRE(decode({ 0x03, 0x34, 0x56, 0x00 }) == byte_vec_t{ 0x34, 0x56 });
  }

  SUBCASE("8 nonzero bytes") {
    REQUIRE(decode({ 0x09, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF, 0x00 }) ==
            byte_vec_t{ 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF });
  }

  SUBCASE("1 zero byte") {
    REQUIRE(decode({ 0x01, 0x01, 0x00 }) == byte_vec_t{ 0x00 });
  }

  SUBCASE("2 zero bytes") {
    REQUIRE(decode({ 0x01, 0x01, 0x01, 0x00 }) == byte_vec_t{ 0x00, 0x00 });
  }

  SUBCASE("8 zero bytes") {
    REQUIRE(decode({ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 }) ==
            byte_vec_t(8, 0x00));
  }

  SUBCASE("4 alternating zero/nonzero bytes") {
    REQUIRE(decode({ 0x01, 0x02, 0x11, 0x02, 0x22, 0x00 }) ==
            byte_vec_t{ 0x00, 0x11, 0x00, 0x22 });
  }

  SUBCASE("4 alternating nonzero/zero bytes") {
    REQUIRE(decode({ 0x02, 0x11, 0x02, 0x22, 0x01, 0x00 }) ==
            byte_vec_t{ 0x11, 0x00, 0x22, 0x00 });
  }
}

TEST_CASE("Decoding 0xFF code blocks") {
  SUBCASE("Exactly 254 nonzero bytes") {
    byte_vec_t enc(255, 0x01);
    enc[0] = 0xFF;
    enc.push_back(0x00);
    REQUIRE(decode(enc) == byte_vec_t(254, 0x01));
  }

  SUBCASE("255 nonzero bytes (two code blocks)") {
    byte_vec_t enc{ 0xFF };
    enc.insert(enc.end(), 254, 0x01);
    enc.push_back(0x02);
    enc.push_back(0x01);
    enc.push_back(0x00);
    REQUIRE(decode(enc) == byte_vec_t(255, 0x01));
  }

  SUBCASE("508 nonzero bytes (two full 0xFF blocks)") {
    byte_vec_t enc{ 0xFF };
    enc.insert(enc.end(), 254, 0xAA);
    enc.push_back(0xFF);
    enc.insert(enc.end(), 254, 0xAA);
    enc.push_back(0x00);
    REQUIRE(decode(enc) == byte_vec_t(508, 0xAA));
  }

  SUBCASE("254 nonzero bytes followed by zero") {
    byte_vec_t dec(254, 0x01);
    dec.push_back(0x00);
    byte_vec_t enc = encode(dec);
    REQUIRE(decode(enc) == dec);
  }
}

TEST_CASE("Decode: known vectors from COBS paper") {
  SUBCASE("Figure 3: IP header fragment") {
    byte_vec_t enc{ 0x02, 0x45, 0x01, 0x04, 0x2C, 0x4C, 0x79,
                    0x01, 0x05, 0x40, 0x06, 0x4F, 0x37, 0x00 };
    byte_vec_t expected{ 0x45, 0x00, 0x00, 0x2C, 0x4C, 0x79,
                         0x00, 0x00, 0x40, 0x06, 0x4F, 0x37 };
    REQUIRE(decode(enc) == expected);
  }
}

TEST_CASE("Decode: Wikipedia examples") {
  SUBCASE("Example 1: single zero byte") {
    REQUIRE(decode({ 0x01, 0x01, 0x00 }) == byte_vec_t{ 0x00 });
  }

  SUBCASE("Example 2: two zero bytes") {
    REQUIRE(decode({ 0x01, 0x01, 0x01, 0x00 }) == byte_vec_t{ 0x00, 0x00 });
  }

  SUBCASE("Example 3: 0x00 0x11 0x00") {
    REQUIRE(decode({ 0x01, 0x02, 0x11, 0x01, 0x00 }) == byte_vec_t{ 0x00, 0x11, 0x00 });
  }

  SUBCASE("Example 4: 0x11 0x22 0x00 0x33") {
    REQUIRE(decode({ 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 }) ==
            byte_vec_t{ 0x11, 0x22, 0x00, 0x33 });
  }

  SUBCASE("Example 5: 0x11 0x22 0x33 0x44") {
    REQUIRE(decode({ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 }) ==
            byte_vec_t{ 0x11, 0x22, 0x33, 0x44 });
  }

  SUBCASE("Example 6: 0x11 0x00 0x00 0x00") {
    REQUIRE(decode({ 0x02, 0x11, 0x01, 0x01, 0x01, 0x00 }) ==
            byte_vec_t{ 0x11, 0x00, 0x00, 0x00 });
  }

  SUBCASE("Example 7: 01..FE (254 nonzero bytes)") {
    byte_vec_t enc(255);
    std::iota(enc.begin(), enc.end(), byte_t{ 0x00 });
    enc[0] = 0xFF;
    enc.push_back(0x00);

    byte_vec_t expected(254);
    std::iota(expected.begin(), expected.end(), byte_t{ 0x01 });
    REQUIRE(decode(enc) == expected);
  }

  SUBCASE("Example 8: 00 01..FE (255 bytes starting with zero)") {
    byte_vec_t enc{ 0x01, 0xFF };
    for (byte_t i{ 0x01 }; i <= 0xFE; ++i) {
      enc.push_back(i);
    }
    enc.push_back(0x00);

    byte_vec_t expected(255);
    std::iota(expected.begin(), expected.end(), byte_t{ 0x00 });
    REQUIRE(decode(enc) == expected);
  }

  SUBCASE("Example 9: 01..FF (255 nonzero bytes)") {
    byte_vec_t enc(255);
    std::iota(enc.begin(), enc.end(), byte_t{ 0x00 });
    enc[0] = 0xFF;
    enc.insert(enc.end(), { 0x02, 0xFF, 0x00 });

    byte_vec_t expected(255);
    std::iota(expected.begin(), expected.end(), byte_t{ 0x01 });
    REQUIRE(decode(enc) == expected);
  }
}

TEST_CASE("Decode: longer payloads") {
  SUBCASE("255 zero bytes") {
    byte_vec_t enc(256, 0x01);
    enc.push_back(0x00);
    REQUIRE(decode(enc) == byte_vec_t(255, 0x00));
  }

  SUBCASE("1024 nonzero bytes") {
    byte_vec_t dec(1024, '!');
    byte_vec_t enc = encode(dec);
    REQUIRE(decode(enc) == dec);
  }

  SUBCASE("1024 zero bytes") {
    byte_vec_t enc(1025, 0x01);
    enc.push_back(0x00);
    REQUIRE(decode(enc) == byte_vec_t(1024, 0x00));
  }

  SUBCASE("1024 alternating zero/nonzero") {
    byte_vec_t dec;
    for (auto i{ 0u }; i < 1024; ++i) {
      dec.push_back(i & 1);
    }
    byte_vec_t enc = encode(dec);
    REQUIRE(decode(enc) == dec);
  }
}

TEST_CASE("Decode: encode/decode round-trips") {
  SUBCASE("Single byte values 0x00..0xFF") {
    for (unsigned b{ 0 }; b <= 0xFF; ++b) {
      byte_vec_t dec{ static_cast<byte_t>(b) };
      REQUIRE(decode(encode(dec)) == dec);
    }
  }

  SUBCASE("Two-byte combinations with zeros") {
    for (unsigned b{ 0 }; b <= 0xFF; ++b) {
      byte_vec_t dec{ 0x00, static_cast<byte_t>(b) };
      REQUIRE(decode(encode(dec)) == dec);
      dec = { static_cast<byte_t>(b), 0x00 };
      REQUIRE(decode(encode(dec)) == dec);
    }
  }

  SUBCASE("Runs of identical bytes") {
    for (unsigned b : { 0x00u, 0x01u, 0x7Fu, 0xFEu, 0xFFu }) {
      for (size_t len : { size_t{ 1 },
                          size_t{ 2 },
                          size_t{ 253 },
                          size_t{ 254 },
                          size_t{ 255 },
                          size_t{ 256 },
                          size_t{ 508 },
                          size_t{ 509 },
                          size_t{ 1000 } }) {
        byte_vec_t dec(len, static_cast<byte_t>(b));
        REQUIRE(decode(encode(dec)) == dec);
      }
    }
  }

  SUBCASE("Ascending byte patterns at boundary lengths") {
    for (size_t len : { size_t{ 1 },
                        size_t{ 253 },
                        size_t{ 254 },
                        size_t{ 255 },
                        size_t{ 256 },
                        size_t{ 508 },
                        size_t{ 512 },
                        size_t{ 1024 } }) {
      byte_vec_t dec(len);
      for (size_t i{ 0 }; i < len; ++i) {
        dec[i] = static_cast<byte_t>(i & 0xFF);
      }
      REQUIRE(decode(encode(dec)) == dec);
    }
  }

  SUBCASE("Zero at every Nth position") {
    for (unsigned n : { 1u, 2u, 127u, 253u, 254u, 255u }) {
      byte_vec_t dec(1024, 0x42);
      for (size_t i{ 0 }; i < dec.size(); i += n) {
        dec[i] = 0x00;
      }
      REQUIRE(decode(encode(dec)) == dec);
    }
  }
}

TEST_CASE("Decode: in-place (output buffer == input buffer)") {
  SUBCASE("Simple") {
    byte_vec_t buf{ 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 };
    size_t dec_len{ 0u };
    REQUIRE(cobs_decode(buf.data(), buf.size(), buf.data(), buf.size(), &dec_len) ==
            COBS_RET_SUCCESS);
    REQUIRE(dec_len == 4);
    REQUIRE(byte_vec_t(buf.data(), buf.data() + dec_len) ==
            byte_vec_t{ 0x11, 0x22, 0x00, 0x33 });
  }

  SUBCASE("254 nonzero bytes") {
    byte_vec_t dec(254, 0x01);
    byte_vec_t buf = encode(dec);
    size_t dec_len{ 0u };
    REQUIRE(cobs_decode(buf.data(), buf.size(), buf.data(), buf.size(), &dec_len) ==
            COBS_RET_SUCCESS);
    REQUIRE(dec_len == 254);
    REQUIRE(byte_vec_t(buf.data(), buf.data() + dec_len) == dec);
  }
}
