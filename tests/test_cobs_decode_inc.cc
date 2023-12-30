#include "../cobs.h"
#include "byte_vec.h"
#include "doctest.h"

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

TEST_CASE("cobs_decode_inc") {
  cobs_decode_inc_ctx_t ctx;
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

  byte_vec_t enc(1024), dec(enc.size() * 2);
  cobs_decode_inc_args_t args{ .src = enc.data(),
                               .dst = dec.data(),
                               .src_max = enc.size(),
                               .dst_max = dec.size() };
  size_t enc_len, dec_len;
  bool done{ false };

  SUBCASE("bad args") {
    REQUIRE(cobs_decode_inc(nullptr, &args, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode_inc(&ctx, nullptr, &enc_len, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode_inc(&ctx, &args, nullptr, &dec_len, &done) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, nullptr, &done) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, nullptr) ==
            COBS_RET_ERR_BAD_ARG);

    SUBCASE("args.src") {
      args.src = nullptr;
      REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
              COBS_RET_ERR_BAD_ARG);
    }
    SUBCASE("args.dst") {
      args.dst = nullptr;
      REQUIRE(cobs_decode_inc(&ctx, &args, &enc_len, &dec_len, &done) ==
              COBS_RET_ERR_BAD_ARG);
    }
  }

  SUBCASE("one byte at a time") {
    dec_len = 512;
    memset(dec.data(), 0xAA, dec_len);
    memset(&dec[10], 0x00, 3);
    memset(&dec[99], 0x00, 5);
    memset(&dec[413], 0x00, 9);

    REQUIRE(cobs_encode(dec.data(), dec_len, enc.data(), enc.size(), &enc_len) ==
            COBS_RET_SUCCESS);
    std::fill(dec.begin(), dec.end(), byte_t{ 0u });
    REQUIRE(enc_len >= dec_len);
    REQUIRE(enc_len <= COBS_ENCODE_MAX(dec_len));

    byte_vec_t oneshot(enc.size());
    size_t oneshot_len;
    REQUIRE(
        cobs_decode(enc.data(), enc_len, oneshot.data(), oneshot.size(), &oneshot_len) ==
        COBS_RET_SUCCESS);
    oneshot.resize(oneshot_len);

    size_t cur_dec{ 0 }, cur_enc{ 0 };
    while (!done) {
      args.src = &enc[cur_enc];
      args.dst = &dec[cur_dec];
      args.src_max = 1;
      args.dst_max = 1;

      size_t this_enc_len, this_dec_len;
      REQUIRE_MESSAGE(cobs_decode_inc(&ctx, &args, &this_enc_len, &this_dec_len, &done) ==
                          COBS_RET_SUCCESS,
                      cur_dec);
      cur_dec += this_dec_len;
      cur_enc += this_enc_len;
    }

    dec.resize(cur_dec);

    REQUIRE(cur_dec == oneshot_len);
    REQUIRE(dec == oneshot);
    REQUIRE(done);
  }
}
