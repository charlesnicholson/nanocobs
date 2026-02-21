#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <algorithm>
#include <cstring>
#include <numeric>

namespace {
byte_vec_t encode(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t enc_len{ 0u };
  byte_t dummy{ 0 };
  void const* src_ptr = dec.empty() ? &dummy : dec.data();
  REQUIRE(cobs_encode(src_ptr, dec.size(), enc.data(), enc.size(), &enc_len) ==
          COBS_RET_SUCCESS);
  enc.resize(enc_len);
  return enc;
}

void verify_frame_invariants(byte_vec_t const& enc) {
  REQUIRE(enc.size() >= 2);
  REQUIRE(enc.back() == 0x00);
  REQUIRE(std::none_of(enc.data(), enc.data() + enc.size() - 1, [](byte_t b) {
    return b == 0;
  }));
}

byte_vec_t decode(byte_vec_t const& enc) {
  byte_vec_t dec(enc.size());
  size_t dec_len{ 0u };
  REQUIRE(cobs_decode(enc.data(), enc.size(), dec.data(), dec.size(), &dec_len) ==
          COBS_RET_SUCCESS);
  dec.resize(dec_len);
  return dec;
}
}  // namespace

TEST_CASE("Encoding validation") {
  byte_t enc[32], dec[32];
  size_t constexpr enc_n{ sizeof(enc) };
  size_t constexpr dec_n{ sizeof(dec) };
  size_t enc_len;

  SUBCASE("Null buffer pointer") {
    REQUIRE(cobs_encode(nullptr, dec_n, enc, enc_n, &enc_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode(dec, dec_n, nullptr, enc_n, &enc_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode(dec, dec_n, enc, enc_n, nullptr) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Invalid enc_max") {
    REQUIRE(cobs_encode(dec, dec_n, enc, 0, &enc_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode(dec, dec_n, enc, 1, &enc_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode(dec, dec_n, enc, dec_n - 2, &enc_len) == COBS_RET_ERR_EXHAUSTED);
    REQUIRE(cobs_encode(dec, dec_n, enc, dec_n - 1, &enc_len) == COBS_RET_ERR_EXHAUSTED);
  }

  SUBCASE("enc_max exactly sufficient") {
    memset(dec, 0x42, 4);
    size_t const needed = COBS_ENCODE_MAX(4);
    REQUIRE(cobs_encode(dec, 4, enc, needed, &enc_len) == COBS_RET_SUCCESS);
    REQUIRE(enc_len == 6);
  }

  SUBCASE("enc_max one byte short") {
    memset(dec, 0x42, 4);
    size_t const needed = COBS_ENCODE_MAX(4);
    REQUIRE(cobs_encode(dec, 4, enc, needed - 1, &enc_len) == COBS_RET_ERR_EXHAUSTED);
  }
}

TEST_CASE("Simple encodings") {
  SUBCASE("Empty") {
    byte_vec_t enc = encode({});
    REQUIRE(enc == byte_vec_t{ 0x01, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("1 nonzero byte") {
    byte_vec_t enc = encode({ 0x34 });
    REQUIRE(enc == byte_vec_t{ 0x02, 0x34, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("2 nonzero bytes") {
    byte_vec_t enc = encode({ 0x34, 0x56 });
    REQUIRE(enc == byte_vec_t{ 0x03, 0x34, 0x56, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("8 nonzero bytes") {
    byte_vec_t enc = encode({ 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF });
    REQUIRE(enc ==
            byte_vec_t{ 0x09, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("1 zero byte") {
    byte_vec_t enc = encode({ 0x00 });
    REQUIRE(enc == byte_vec_t{ 0x01, 0x01, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("2 zero bytes") {
    byte_vec_t enc = encode({ 0x00, 0x00 });
    REQUIRE(enc == byte_vec_t{ 0x01, 0x01, 0x01, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("8 zero bytes") {
    byte_vec_t enc = encode(byte_vec_t(8, 0x00));
    REQUIRE(enc ==
            byte_vec_t{ 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("4 alternating zero/nonzero bytes") {
    byte_vec_t enc = encode({ 0x00, 0x11, 0x00, 0x22 });
    REQUIRE(enc == byte_vec_t{ 0x01, 0x02, 0x11, 0x02, 0x22, 0x00 });
    verify_frame_invariants(enc);
  }

  SUBCASE("4 alternating nonzero/zero bytes") {
    byte_vec_t enc = encode({ 0x11, 0x00, 0x22, 0x00 });
    REQUIRE(enc == byte_vec_t{ 0x02, 0x11, 0x02, 0x22, 0x01, 0x00 });
    verify_frame_invariants(enc);
  }
}

TEST_CASE("Encoding 0xFF code blocks") {
  SUBCASE("253 nonzero bytes (just under 0xFF threshold)") {
    byte_vec_t enc = encode(byte_vec_t(253, 0x42));
    REQUIRE(enc[0] == 0xFE);
    verify_frame_invariants(enc);
    REQUIRE(decode(enc) == byte_vec_t(253, 0x42));
  }

  SUBCASE("Exactly 254 nonzero bytes (single 0xFF block)") {
    byte_vec_t expected(255, 0x01);
    expected[0] = 0xFF;
    expected.push_back(0x00);
    byte_vec_t enc = encode(byte_vec_t(254, 0x01));
    REQUIRE(enc == expected);
    verify_frame_invariants(enc);
  }

  SUBCASE("255 nonzero bytes (two code blocks)") {
    byte_vec_t expected{ 0xFF };
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0x02);
    expected.push_back(0x01);
    expected.push_back(0x00);
    byte_vec_t enc = encode(byte_vec_t(255, 0x01));
    REQUIRE(enc == expected);
    verify_frame_invariants(enc);
  }

  SUBCASE("256 nonzero bytes (0xFF block + 2)") {
    byte_vec_t enc = encode(byte_vec_t(256, 0x01));
    REQUIRE(enc[0] == 0xFF);
    verify_frame_invariants(enc);
    REQUIRE(decode(enc) == byte_vec_t(256, 0x01));
  }

  SUBCASE("508 nonzero bytes (two full 0xFF blocks)") {
    byte_vec_t expected{ 0xFF };
    expected.insert(std::end(expected), 254, 0xAA);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0xAA);
    expected.push_back(0x00);
    byte_vec_t enc = encode(byte_vec_t(508, 0xAA));
    REQUIRE(enc == expected);
    verify_frame_invariants(enc);
  }

  SUBCASE("254 nonzero bytes followed by zero") {
    byte_vec_t dec(254, 0x01);
    dec.push_back(0x00);
    byte_vec_t enc = encode(dec);
    verify_frame_invariants(enc);
    REQUIRE(decode(enc) == dec);
  }
}

TEST_CASE("Encode: Wikipedia examples") {
  SUBCASE("Example 1: single zero byte") {
    REQUIRE(encode({ 0x00 }) == byte_vec_t{ 0x01, 0x01, 0x00 });
  }

  SUBCASE("Example 2: two zero bytes") {
    REQUIRE(encode({ 0x00, 0x00 }) == byte_vec_t{ 0x01, 0x01, 0x01, 0x00 });
  }

  SUBCASE("Example 3: 0x00 0x11 0x00") {
    REQUIRE(encode({ 0x00, 0x11, 0x00 }) == byte_vec_t{ 0x01, 0x02, 0x11, 0x01, 0x00 });
  }

  SUBCASE("Example 4: 0x11 0x22 0x00 0x33") {
    REQUIRE(encode({ 0x11, 0x22, 0x00, 0x33 }) ==
            byte_vec_t{ 0x03, 0x11, 0x22, 0x02, 0x33, 0x00 });
  }

  SUBCASE("Example 5: 0x11 0x22 0x33 0x44") {
    REQUIRE(encode({ 0x11, 0x22, 0x33, 0x44 }) ==
            byte_vec_t{ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 });
  }

  SUBCASE("Example 6: 0x11 0x00 0x00 0x00") {
    REQUIRE(encode({ 0x11, 0x00, 0x00, 0x00 }) ==
            byte_vec_t{ 0x02, 0x11, 0x01, 0x01, 0x01, 0x00 });
  }

  SUBCASE("Example 7: 01..FE (254 nonzero bytes)") {
    byte_vec_t dec(254);
    std::iota(dec.begin(), dec.end(), byte_t{ 0x01 });
    byte_vec_t expected(255);
    std::iota(expected.begin(), expected.end(), byte_t{ 0x00 });
    expected[0] = 0xFF;
    expected.push_back(0x00);
    REQUIRE(encode(dec) == expected);
  }

  SUBCASE("Example 8: 00 01..FE (255 bytes starting with zero)") {
    byte_vec_t dec(255);
    std::iota(dec.begin(), dec.end(), byte_t{ 0x00 });
    byte_vec_t expected{ 0x01, 0xFF };
    for (byte_t i{ 0x01 }; i <= 0xFE; ++i) {
      expected.push_back(i);
    }
    expected.push_back(0x00);
    REQUIRE(encode(dec) == expected);
  }

  SUBCASE("Example 9: 01..FF (255 nonzero bytes)") {
    byte_vec_t dec(255);
    std::iota(dec.begin(), dec.end(), byte_t{ 0x01 });
    byte_vec_t expected(255);
    std::iota(expected.begin(), expected.end(), byte_t{ 0x00 });
    expected[0] = 0xFF;
    expected.insert(expected.end(), { 0x02, 0xFF, 0x00 });
    REQUIRE(encode(dec) == expected);
  }
}

TEST_CASE("Encode: known vectors from COBS paper") {
  SUBCASE("Figure 3: IP header fragment") {
    byte_vec_t dec{
      0x45, 0x00, 0x00, 0x2C, 0x4C, 0x79, 0x00, 0x00, 0x40, 0x06, 0x4F, 0x37
    };
    byte_vec_t expected{ 0x02, 0x45, 0x01, 0x04, 0x2C, 0x4C, 0x79,
                         0x01, 0x05, 0x40, 0x06, 0x4F, 0x37, 0x00 };
    REQUIRE(encode(dec) == expected);
  }
}

TEST_CASE("Longer payload encodings") {
  SUBCASE("255 zero bytes") {
    byte_vec_t expected(256, 0x01);
    expected.push_back(0x00);
    REQUIRE(encode(byte_vec_t(255, 0x00)) == expected);
  }

  SUBCASE("1024 nonzero bytes") {
    byte_vec_t expected;
    for (auto i{ 0u }; i < 1024 / 254; ++i) {
      expected.push_back(0xFF);
      expected.insert(std::end(expected), 254, '!');
    }
    expected.push_back((1024 % 254) + 1);
    expected.insert(std::end(expected), (1024 % 254), '!');
    expected.push_back(0x00);
    REQUIRE(encode(byte_vec_t(1024, '!')) == expected);
  }

  SUBCASE("1024 zero bytes") {
    byte_vec_t expected(1025, 0x01);
    expected.push_back(0x00);
    REQUIRE(encode(byte_vec_t(1024, 0x00)) == expected);
  }

  SUBCASE("1024 every other byte is zero") {
    byte_vec_t dec;
    for (auto i{ 0u }; i < 1024; ++i) {
      dec.push_back(i & 1);
    }

    byte_vec_t expected;
    for (auto i{ 0u }; i <= 1024; ++i) {
      expected.push_back((i & 1) ? 2 : 1);
    }
    expected.push_back(0x00);
    REQUIRE(encode(dec) == expected);
  }

  SUBCASE("Ascending byte pattern") {
    byte_vec_t dec(512);
    std::iota(dec.begin(), dec.end(), byte_t{ 0x00 });
    byte_vec_t enc = encode(dec);
    verify_frame_invariants(enc);
    REQUIRE(decode(enc) == dec);
  }
}

TEST_CASE("Encode: frame invariants") {
  SUBCASE("Single byte values 0x00..0xFF") {
    for (unsigned b{ 0 }; b <= 0xFF; ++b) {
      verify_frame_invariants(encode({ static_cast<byte_t>(b) }));
    }
  }

  SUBCASE("Runs of identical bytes at boundary lengths") {
    for (unsigned b : { 0x00u, 0x01u, 0x7Fu, 0xFEu, 0xFFu }) {
      for (size_t len : { size_t{ 1 },
                          size_t{ 2 },
                          size_t{ 253 },
                          size_t{ 254 },
                          size_t{ 255 },
                          size_t{ 256 },
                          size_t{ 508 },
                          size_t{ 509 } }) {
        verify_frame_invariants(encode(byte_vec_t(len, static_cast<byte_t>(b))));
      }
    }
  }
}

TEST_CASE("Encode: round-trip encode/decode") {
  SUBCASE("Single byte values") {
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

  SUBCASE("Runs at boundary lengths") {
    for (unsigned b : { 0x00u, 0x01u, 0xFFu }) {
      for (size_t len : { size_t{ 1 },
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
