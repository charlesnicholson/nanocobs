#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <algorithm>
#include <cstring>
#include <numeric>

TEST_CASE("cobs_decode_inc_begin") {
  REQUIRE(cobs_decode_inc_begin(nullptr) == COBS_RET_ERR_BAD_ARG);

  cobs_decode_inc_ctx_t c;
  c.state = cobs_decode_inc_ctx_t::cobs_decode_inc_state(
      cobs_decode_inc_ctx_t::COBS_DECODE_READ_CODE + 1);
  REQUIRE(cobs_decode_inc_begin(&c) == COBS_RET_SUCCESS);
  REQUIRE(c.state == cobs_decode_inc_ctx_t::COBS_DECODE_READ_CODE);
}

TEST_CASE("cobs_decode_inc bad args") {
  cobs_decode_inc_ctx_t ctx{};
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

  byte_vec_t enc(64), dec(64);
  cobs_decode_inc_args_t args{ .enc_src = enc.data(),
                               .dec_dst = dec.data(),
                               .enc_src_max = enc.size(),
                               .dec_dst_max = dec.size() };
  size_t enc_len{ 0u }, dec_len{ 0u };
  bool done{ false };

  SUBCASE("null ctx") {
    REQUIRE(cobs_decode_inc(nullptr, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null args") {
    REQUIRE(cobs_decode_inc(&ctx, nullptr, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null out_enc_src_len") {
    REQUIRE(cobs_decode_inc(&ctx, &args, nullptr, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null out_dec_dst_len") {
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, nullptr, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null out_decode_complete") {
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, nullptr) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null args.enc_src") {
    args.enc_src = nullptr;
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
  SUBCASE("null args.dec_dst") {
    args.dec_dst = nullptr;
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
  }
}

namespace {

byte_vec_t do_encode(byte_vec_t const& src) {
  byte_vec_t enc(COBS_ENCODE_MAX(src.size()));
  size_t enc_len{ 0u };
  // Empty vector .data() may be null; use a dummy byte for the pointer.
  byte_t dummy{ 0 };
  void const* src_ptr = src.empty() ? &dummy : src.data();
  REQUIRE(cobs_encode(src_ptr, src.size(), enc.data(), enc.size(), &enc_len) ==
          COBS_RET_SUCCESS);
  enc.resize(enc_len);
  return enc;
}

byte_vec_t do_decode_oneshot(byte_vec_t const& enc, size_t max_dec) {
  byte_vec_t dec(max_dec);
  size_t dec_len{ 0u };
  REQUIRE(cobs_decode(enc.data(), enc.size(), dec.data(), dec.size(), &dec_len) ==
          COBS_RET_SUCCESS);
  dec.resize(dec_len);
  return dec;
}

// Incrementally decode |enc| using fixed chunk sizes, compare against |expected|.
void verify_inc_decode(byte_vec_t const& enc,
                       byte_vec_t const& expected,
                       size_t enc_chunk,
                       size_t dec_chunk) {
  cobs_decode_inc_ctx_t ctx;
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

  byte_vec_t dec(expected.size() + 16);  // a little extra room
  size_t cur_enc{ 0 }, cur_dec{ 0 };
  bool done{ false };

  while (!done) {
    cobs_decode_inc_args_t args;
    args.enc_src = enc.data() + cur_enc;
    args.dec_dst = dec.data() + cur_dec;
    args.enc_src_max = std::min(enc_chunk, enc.size() - cur_enc);
    args.dec_dst_max = std::min(dec_chunk, dec.size() - cur_dec);

    size_t this_enc{ 0u }, this_dec{ 0u };
    REQUIRE(cobs_decode_inc(&ctx, &args, &this_enc, &this_dec, &done) == COBS_RET_SUCCESS);
    cur_enc += this_enc;
    cur_dec += this_dec;
  }

  REQUIRE(cur_dec == expected.size());
  REQUIRE(byte_vec_t(dec.data(), dec.data() + cur_dec) == expected);
}

// Test incremental decode of |src| at multiple chunk size combos.
void verify_inc_round_trip(byte_vec_t const& src) {
  byte_vec_t const enc = do_encode(src);
  byte_vec_t const expected = do_decode_oneshot(enc, src.size() + 1);
  REQUIRE(expected == src);

  struct {
    size_t enc_chunk;
    size_t dec_chunk;
  } const combos[] = {
    { enc.size(), expected.size() + 1 },  // full
    { 1, expected.size() + 1 },           // 1-byte enc
    { enc.size(), 1 },                    // 1-byte dec
    { 1, 1 },                             // 1-byte both
    { 19, 29 },                           // coprime
    { 254, 254 },                         // at code block boundary
    { 255, 253 },                         // straddling code block boundary
  };

  for (auto const& c : combos) {
    verify_inc_decode(enc, expected, c.enc_chunk, c.dec_chunk);
  }
}

}  // namespace

TEST_CASE("cobs_decode_inc: empty payload") {
  verify_inc_round_trip({});
}

TEST_CASE("cobs_decode_inc: single byte payloads") {
  SUBCASE("zero") {
    verify_inc_round_trip({ 0x00 });
  }
  SUBCASE("nonzero") {
    verify_inc_round_trip({ 0x42 });
  }
  SUBCASE("0xFF") {
    verify_inc_round_trip({ 0xFF });
  }
}

TEST_CASE("cobs_decode_inc: small payloads") {
  SUBCASE("two zeros") {
    verify_inc_round_trip({ 0x00, 0x00 });
  }
  SUBCASE("two nonzero") {
    verify_inc_round_trip({ 0x11, 0x22 });
  }
  SUBCASE("zero then nonzero") {
    verify_inc_round_trip({ 0x00, 0x42 });
  }
  SUBCASE("nonzero then zero") {
    verify_inc_round_trip({ 0x42, 0x00 });
  }

  SUBCASE("4 alternating zero/nonzero") {
    verify_inc_round_trip({ 0x00, 0x11, 0x00, 0x22 });
  }
  SUBCASE("4 alternating nonzero/zero") {
    verify_inc_round_trip({ 0x11, 0x00, 0x22, 0x00 });
  }
}

TEST_CASE("cobs_decode_inc: 0xFF code block boundaries") {
  SUBCASE("253 nonzero bytes") {
    verify_inc_round_trip(byte_vec_t(253, 0x01));
  }
  SUBCASE("254 nonzero bytes (exactly one 0xFF block)") {
    verify_inc_round_trip(byte_vec_t(254, 0x01));
  }
  SUBCASE("255 nonzero bytes (0xFF block + 1)") {
    verify_inc_round_trip(byte_vec_t(255, 0x01));
  }
  SUBCASE("256 nonzero bytes") {
    verify_inc_round_trip(byte_vec_t(256, 0x01));
  }
  SUBCASE("508 nonzero bytes (two full 0xFF blocks)") {
    verify_inc_round_trip(byte_vec_t(508, 0xAA));
  }
  SUBCASE("509 nonzero bytes") {
    verify_inc_round_trip(byte_vec_t(509, 0xAA));
  }
}

TEST_CASE("cobs_decode_inc: all-zero payloads") {
  SUBCASE("1 zero") {
    verify_inc_round_trip(byte_vec_t(1, 0x00));
  }
  SUBCASE("254 zeros") {
    verify_inc_round_trip(byte_vec_t(254, 0x00));
  }
  SUBCASE("255 zeros") {
    verify_inc_round_trip(byte_vec_t(255, 0x00));
  }
  SUBCASE("1024 zeros") {
    verify_inc_round_trip(byte_vec_t(1024, 0x00));
  }
}

TEST_CASE("cobs_decode_inc: mixed patterns") {
  SUBCASE("ascending bytes") {
    byte_vec_t src(512);
    std::iota(src.begin(), src.end(), byte_t{ 0x00 });
    verify_inc_round_trip(src);
  }

  SUBCASE("zero runs in nonzero data") {
    byte_vec_t src(900, 0xAA);
    memset(src.data() + 10, 0x00, 3);
    memset(src.data() + 99, 0x00, 5);
    memset(src.data() + 413, 0x00, 9);
    std::iota(src.data() + 500, src.data() + 800, byte_t{ 0x00 });
    verify_inc_round_trip(src);
  }

  SUBCASE("nonzero run spanning two 0xFF blocks") {
    byte_vec_t src(600, 0xBB);
    src[0] = 0x00;
    src[599] = 0x00;
    verify_inc_round_trip(src);
  }

  SUBCASE("zero at every 254th position") {
    byte_vec_t src(1024, 0x42);
    for (size_t i{ 0 }; i < src.size(); i += 254) {
      src[i] = 0x00;
    }
    verify_inc_round_trip(src);
  }
}

TEST_CASE("cobs_decode_inc: bad payload detection") {
  cobs_decode_inc_ctx_t ctx;
  byte_t dec[64];
  size_t enc_len{ 0u }, dec_len{ 0u };
  bool done{ false };

  SUBCASE("interior zero in run") {
    REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);
    // Code 0x04 says 3 data bytes follow, but second byte is zero
    byte_t enc[] = { 0x04, 0x11, 0x00, 0x33, 0x00 };
    cobs_decode_inc_args_t args{ .enc_src = enc,
                                 .dec_dst = dec,
                                 .enc_src_max = sizeof(enc),
                                 .dec_dst_max = sizeof(dec) };
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("interior zero fed one byte at a time") {
    REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);
    // Feed code byte 0x03 (2 data bytes expected)
    byte_t b0[] = { 0x03 };
    cobs_decode_inc_args_t args{ .enc_src = b0,
                                 .dec_dst = dec,
                                 .enc_src_max = 1,
                                 .dec_dst_max = sizeof(dec) };
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) == COBS_RET_SUCCESS);
    REQUIRE(!done);

    // Feed a zero where a nonzero data byte is expected
    byte_t b1[] = { 0x00 };
    args.enc_src = b1;
    args.enc_src_max = 1;
    args.dec_dst = dec + dec_len;
    args.dec_dst_max = sizeof(dec) - dec_len;
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_PAYLOAD);
  }
}

TEST_CASE("cobs_decode_inc: output buffer pressure") {
  // Encode a payload, then decode with a tiny output buffer to force
  // many incremental calls with partial output progress.
  byte_vec_t src(512);
  std::iota(src.begin(), src.end(), byte_t{ 0x00 });
  byte_vec_t const enc = do_encode(src);

  cobs_decode_inc_ctx_t ctx;
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

  byte_vec_t dec(src.size());
  size_t cur_enc{ 0 }, cur_dec{ 0 };
  bool done{ false };

  // Feed all encoded bytes at once, but only allow 1 decoded byte at a time
  while (!done) {
    cobs_decode_inc_args_t args;
    args.enc_src = enc.data() + cur_enc;
    args.dec_dst = dec.data() + cur_dec;
    args.enc_src_max = enc.size() - cur_enc;
    args.dec_dst_max = 1;

    size_t this_enc{ 0u }, this_dec{ 0u };
    REQUIRE(cobs_decode_inc(&ctx, &args, &this_enc, &this_dec, &done) == COBS_RET_SUCCESS);
    cur_enc += this_enc;
    cur_dec += this_dec;
    REQUIRE(this_dec <= 1);
  }

  REQUIRE(cur_dec == src.size());
  REQUIRE(dec == src);
}

TEST_CASE("cobs_decode_inc: source buffer pressure") {
  // Encode a payload, then feed encoded data 1 byte at a time with a large
  // output buffer, to test state machine transitions at every byte boundary.
  byte_vec_t src(512, 0xCC);
  for (size_t i{ 0 }; i < src.size(); i += 50) {
    src[i] = 0x00;
  }
  byte_vec_t const enc = do_encode(src);

  cobs_decode_inc_ctx_t ctx;
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

  byte_vec_t dec(src.size());
  size_t cur_enc{ 0 }, cur_dec{ 0 };
  bool done{ false };

  while (!done) {
    cobs_decode_inc_args_t args;
    args.enc_src = enc.data() + cur_enc;
    args.dec_dst = dec.data() + cur_dec;
    args.enc_src_max = 1;
    args.dec_dst_max = dec.size() - cur_dec;

    size_t this_enc{ 0u }, this_dec{ 0u };
    REQUIRE(cobs_decode_inc(&ctx, &args, &this_enc, &this_dec, &done) == COBS_RET_SUCCESS);
    cur_enc += this_enc;
    cur_dec += this_dec;
    REQUIRE(this_enc <= 1);
  }

  REQUIRE(cur_dec == src.size());
  REQUIRE(dec == src);
}
