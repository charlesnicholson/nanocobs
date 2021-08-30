#include "../cobs.h"
#include "doctest.h"

#include <algorithm>
#include <vector>


TEST_CASE("cobs_encode_inc_begin") {
  cobs_enc_ctx_t ctx;
  std::vector<unsigned char> buf(1024);
  unsigned const n = static_cast<unsigned>(buf.size());

  SUBCASE("bad args") {
    REQUIRE(cobs_encode_inc_begin(nullptr, n, &ctx) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(buf.data(), 0, &ctx) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(buf.data(), 1, &ctx) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(buf.data(), 2, nullptr) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("initializes context") {
    ctx.cur = 123;
    ctx.code_idx = 456;
    ctx.code = 789;
    REQUIRE(cobs_encode_inc_begin(buf.data(), n, &ctx) == COBS_RET_SUCCESS);
    REQUIRE(ctx.dst == buf.data());
    REQUIRE(ctx.dst_max == n);
    REQUIRE(ctx.cur == 1);
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.code_idx == 0);
  }
}


TEST_CASE("cobs_encode_inc") {
  cobs_enc_ctx_t ctx;
  unsigned const enc_max{1024};
  std::vector<unsigned char> enc_buf(enc_max);
  unsigned const dec_max{1024};
  std::vector<unsigned char> dec_buf(dec_max);

  REQUIRE(cobs_encode_inc_begin(enc_buf.data(), enc_max, &ctx) == COBS_RET_SUCCESS);

  SUBCASE("bad args") {
    REQUIRE(cobs_encode_inc(nullptr, dec_buf.data(), 16) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc(&ctx, nullptr, 16) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("exhausted") {
    ctx.cur = ctx.dst_max - 1;
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 1) == COBS_RET_ERR_EXHAUSTED);
    ctx.cur = ctx.dst_max - 2;
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 2) == COBS_RET_ERR_EXHAUSTED);
    ctx.cur = 0;
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), ctx.dst_max) == COBS_RET_ERR_EXHAUSTED);
  }

  SUBCASE("accumulates nonzero bytes into buffer") {
    dec_buf[0] = 0x12;
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 1) == COBS_RET_SUCCESS);
    REQUIRE(enc_buf[1] == 0x12);

    for (unsigned char i = 0u; i < 10u; ++i) { dec_buf[i] = i; }
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 10) == COBS_RET_SUCCESS);
    for (unsigned char i = 0u; i < 10u; ++i) { REQUIRE(enc_buf[2 + i] == i); }
  }

  SUBCASE("advances cursor with every byte written") {
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 1) == COBS_RET_SUCCESS);
    REQUIRE(ctx.cur == 2);
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 12) == COBS_RET_SUCCESS);
    REQUIRE(ctx.cur == 14);
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 37) == COBS_RET_SUCCESS);
    REQUIRE(ctx.cur == 51);
  }

  SUBCASE("Nonzero bytes increment code") {
    std::fill(std::begin(dec_buf), std::end(dec_buf), 1);
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 1) == COBS_RET_SUCCESS);
    REQUIRE(ctx.code == 2);
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 13) == COBS_RET_SUCCESS);
    REQUIRE(ctx.code == 15);
    REQUIRE(cobs_encode_inc(&ctx, dec_buf.data(), 59) == COBS_RET_SUCCESS);
    REQUIRE(ctx.code == 74);
  }

}
