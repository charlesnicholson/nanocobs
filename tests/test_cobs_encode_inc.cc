#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <algorithm>
#include <numeric>
#include <random>

TEST_CASE("cobs_encode_inc_begin") {
  cobs_enc_ctx_t ctx{};
  byte_vec_t work_buf(255);

  SUBCASE("bad args") {
    REQUIRE(cobs_encode_inc_begin(nullptr, work_buf.data(), work_buf.size()) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(&ctx, nullptr, 255) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), 254) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), 0) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("buf_len of 255 succeeds") {
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), 255) == COBS_RET_SUCCESS);
  }

  SUBCASE("initializes context") {
    ctx.code = 123;
    ctx.buf_len = 99;
    ctx.flush_pos = 42;
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
            COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.buf == work_buf.data());
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.buf_len == 1);
    REQUIRE(ctx.flush_pos == 0);
  }
}

TEST_CASE("cobs_encode_inc") {
  cobs_enc_ctx_t ctx{};
  byte_vec_t work_buf(255);
  byte_vec_t enc_buf(1024);
  byte_vec_t dec_buf(1024);
  REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
          COBS_RET_SUCCESS);

  SUBCASE("bad args") {
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 16;
    args.enc_dst_max = 16;

    REQUIRE(cobs_encode_inc(nullptr, &args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc(&ctx, nullptr, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc(&ctx, &args, nullptr, &dst_len) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, nullptr) == COBS_RET_ERR_BAD_ARG);

    cobs_encode_inc_args_t bad_args{};
    bad_args.dec_src = nullptr;
    bad_args.enc_dst = enc_buf.data();
    bad_args.dec_src_max = 16;
    bad_args.enc_dst_max = 16;
    REQUIRE(cobs_encode_inc(&ctx, &bad_args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);

    bad_args.dec_src = dec_buf.data();
    bad_args.enc_dst = nullptr;
    REQUIRE(cobs_encode_inc(&ctx, &bad_args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("zero-length source consumes nothing") {
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 0;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 0);
    REQUIRE(dst_len == 0);
  }

  SUBCASE("accumulates nonzero bytes without flushing") {
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x34;
    dec_buf[2] = 0x56;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 3;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 3);
    REQUIRE(dst_len == 0);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 4);
    REQUIRE(ctx.buf_len == 4);
  }

  SUBCASE("flushes block on zero byte") {
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x00;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 2;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 2);
    REQUIRE(dst_len == 2);
    REQUIRE(enc_buf[0] == 0x02);
    REQUIRE(enc_buf[1] == 0x12);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.buf_len == 1);
    REQUIRE(ctx.prev_was_ff == 0);
  }

  SUBCASE("flushes block at 0xFF boundary") {
    std::fill(dec_buf.begin(), dec_buf.begin() + 254, byte_t{ 0x01 });
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 254;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 254);
    REQUIRE(dst_len == 255);
    REQUIRE(enc_buf[0] == 0xFF);
    for (size_t i = 1; i < 255; ++i) {
      REQUIRE(enc_buf[i] == 0x01);
    }
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.buf_len == 1);
    REQUIRE(ctx.prev_was_ff == 1);
  }

  SUBCASE("prev_was_ff cleared by non-0xFF block") {
    // 254 nonzero bytes → 0xFF block, then a zero byte → non-0xFF block
    std::fill(dec_buf.begin(), dec_buf.begin() + 254, byte_t{ 0x01 });
    dec_buf[254] = 0x00;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 255;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.prev_was_ff == 0);
  }

  SUBCASE("rejects calls after end (DONE state)") {
    size_t dst_len{};
    bool finished{};
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);

    size_t src_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 1;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("rejects calls in FLUSH_FINAL state") {
    // Transition to FLUSH_FINAL via end() with zero output
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, nullptr, nullptr) !=
            COBS_RET_SUCCESS);  // bad arg (nullptr), but let's do it properly:
    size_t dst_len{};
    bool finished{};
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSH_FINAL);

    size_t src_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 1;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("rejects calls in WRITE_DELIM state") {
    // Transition to WRITE_DELIM: end() with enough room for final code but not delimiter
    size_t dst_len{};
    bool finished{};
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_WRITE_DELIM);

    size_t src_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 1;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("partial flush when output buffer is small") {
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x00;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 2;
    args.enc_dst_max = 1;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 2);
    REQUIRE(dst_len == 1);
    REQUIRE(enc_buf[0] == 0x02);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);

    // Continue flushing with another call
    cobs_encode_inc_args_t args2{};
    args2.dec_src = dec_buf.data();
    args2.enc_dst = enc_buf.data() + 1;
    args2.dec_src_max = 0;
    args2.enc_dst_max = 1;
    REQUIRE(cobs_encode_inc(&ctx, &args2, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(dst_len == 1);
    REQUIRE(enc_buf[1] == 0x12);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
  }

  SUBCASE("flush completes then accumulates more source in same call") {
    // Trigger a block [0x12, 0x00], partially flush (1 of 2 bytes)
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x00;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 2;
    args.enc_dst_max = 1;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);

    // Now call with output room AND new source — should finish flush then consume source
    dec_buf[0] = 0x34;
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data() + 1;
    args.dec_src_max = 1;
    args.enc_dst_max = 10;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 1);        // source byte consumed
    REQUIRE(dst_len == 1);        // remaining flush byte written
    REQUIRE(enc_buf[1] == 0x12);  // flushed data byte
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 2);  // 0x34 accumulated
    REQUIRE(ctx.buf_len == 2);
  }

  SUBCASE("zero output space accumulates without flushing") {
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x34;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 2;
    args.enc_dst_max = 0;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 2);
    REQUIRE(dst_len == 0);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 3);
    REQUIRE(ctx.buf_len == 3);
  }

  SUBCASE("zero output space with block completion queues flush") {
    dec_buf[0] = 0x12;
    dec_buf[1] = 0x00;
    dec_buf[2] = 0x34;  // should NOT be consumed
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 3;
    args.enc_dst_max = 0;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 2);  // consumed up to and including the zero
    REQUIRE(dst_len == 0);  // no output (couldn't flush)
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);
    REQUIRE(ctx.flush_pos == 0);  // nothing flushed yet
  }

  SUBCASE("multiple blocks flushed in single call") {
    // [0x11, 0x00, 0x22, 0x00] = two blocks in one call
    dec_buf[0] = 0x11;
    dec_buf[1] = 0x00;
    dec_buf[2] = 0x22;
    dec_buf[3] = 0x00;
    size_t src_len{}, dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec_buf.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 4;
    args.enc_dst_max = 1024;
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    REQUIRE(src_len == 4);
    REQUIRE(dst_len == 4);  // block1: [0x02, 0x11], block2: [0x02, 0x22]
    REQUIRE(enc_buf[0] == 0x02);
    REQUIRE(enc_buf[1] == 0x11);
    REQUIRE(enc_buf[2] == 0x02);
    REQUIRE(enc_buf[3] == 0x22);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
  }
}

TEST_CASE("cobs_encode_inc_end") {
  cobs_enc_ctx_t ctx{};
  byte_vec_t work_buf(255);
  byte_vec_t enc_buf(1024);
  size_t dst_len{};
  bool finished{};

  REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
          COBS_RET_SUCCESS);

  SUBCASE("bad args") {
    REQUIRE(cobs_encode_inc_end(nullptr,
                                enc_buf.data(),
                                enc_buf.size(),
                                &dst_len,
                                &finished) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_end(&ctx, nullptr, enc_buf.size(), &dst_len, &finished) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), nullptr, &finished) ==
        COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, nullptr) ==
            COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("empty payload produces valid frame") {
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 2);
    REQUIRE(enc_buf[0] == 0x01);
    REQUIRE(enc_buf[1] == 0x00);
  }

  SUBCASE("writes final code + data + delimiter") {
    // Feed some nonzero bytes first
    byte_vec_t dec{ 0x12, 0x34 };
    size_t src_len{};
    size_t inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(inc_dst_len == 0);

    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 4);
    REQUIRE(enc_buf[0] == 0x03);
    REQUIRE(enc_buf[1] == 0x12);
    REQUIRE(enc_buf[2] == 0x34);
    REQUIRE(enc_buf[3] == 0x00);
  }

  SUBCASE("end with 1-byte output buffer needs multiple calls") {
    byte_vec_t dec{ 0xAA, 0xBB };
    size_t src_len{};
    size_t inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);

    // Flush with 1-byte output at a time
    byte_vec_t result;
    finished = false;
    while (!finished) {
      byte_t out_byte{};
      REQUIRE(cobs_encode_inc_end(&ctx, &out_byte, 1, &dst_len, &finished) ==
              COBS_RET_SUCCESS);
      for (size_t i = 0; i < dst_len; ++i) {
        result.push_back((&out_byte)[i]);
      }
    }
    REQUIRE(result.size() == 4);
    REQUIRE(result[0] == 0x03);
    REQUIRE(result[1] == 0xAA);
    REQUIRE(result[2] == 0xBB);
    REQUIRE(result[3] == 0x00);
  }

  SUBCASE("zero-size output makes no progress") {
    // Empty payload: ACCUMULATE → FLUSH_FINAL (buf=[0x01]), but 0 output room
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(dst_len == 0);
    REQUIRE(!finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSH_FINAL);
  }

  SUBCASE("end state progression: ACCUMULATE to DONE") {
    // Feed a byte so FLUSH_FINAL has something to flush
    byte_vec_t dec{ 0x42 };
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 1;
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);

    // 1 byte output: ACCUMULATE → FLUSH_FINAL, flush 1 of 2 bytes
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(dst_len == 1);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSH_FINAL);

    // 1 byte output: finish FLUSH_FINAL → WRITE_DELIM, no room for delimiter
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data() + 1, 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(dst_len == 1);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_WRITE_DELIM);

    // 1 byte output: write delimiter → DONE
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data() + 2, 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 1);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);
    REQUIRE(enc_buf[0] == 0x02);
    REQUIRE(enc_buf[1] == 0x42);
    REQUIRE(enc_buf[2] == 0x00);
  }

  SUBCASE("DONE is sticky") {
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);

    // Repeated calls return finished with zero bytes written
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 0);
  }

  SUBCASE("end picks up partial flush left by inc") {
    // Create a block, flush partially via inc, then call end
    byte_vec_t dec{ 0x11, 0x22, 0x00 };
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = 1;  // only room for 1 byte
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);

    // end() should finish the flush, then finalize
    byte_vec_t result(enc_buf.data(), enc_buf.data() + inc_dst_len);
    finished = false;
    while (!finished) {
      byte_t out_byte{};
      REQUIRE(cobs_encode_inc_end(&ctx, &out_byte, 1, &dst_len, &finished) ==
              COBS_RET_SUCCESS);
      for (size_t i = 0; i < dst_len; ++i) {
        result.push_back((&out_byte)[i]);
      }
    }
    // [0x11, 0x22, 0x00] encodes to [0x03, 0x11, 0x22, 0x01, 0x00]
    REQUIRE(result.size() == 5);
    REQUIRE(result[0] == 0x03);
    REQUIRE(result[1] == 0x11);
    REQUIRE(result[2] == 0x22);
    REQUIRE(result[3] == 0x01);
    REQUIRE(result[4] == 0x00);
  }

  SUBCASE("begin reinitializes a completed context") {
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);

    // Reinitialize
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
            COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_ACCUMULATE);
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.buf_len == 1);
    REQUIRE(ctx.flush_pos == 0);
    REQUIRE(ctx.prev_was_ff == 0);

    // Should be fully functional again
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 2);
    REQUIRE(enc_buf[0] == 0x01);
    REQUIRE(enc_buf[1] == 0x00);
  }

  SUBCASE("prev_was_ff shortcut skips FLUSH_FINAL") {
    // Feed 254 nonzero bytes → 0xFF block flushes, prev_was_ff=1, code=1, buf_len=1
    byte_vec_t dec(254, 0x01);
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.prev_was_ff == 1);
    REQUIRE(ctx.code == 1);
    REQUIRE(ctx.buf_len == 1);

    // end() should skip FLUSH_FINAL and go directly to WRITE_DELIM → DONE
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data() + inc_dst_len, 1, &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 1);
    REQUIRE(enc_buf[inc_dst_len] == 0x00);  // delimiter only, no trailing code byte
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);
  }

  SUBCASE("prev_was_ff with accumulated data goes to FLUSH_FINAL") {
    // Feed 254+3 nonzero bytes: 0xFF block, then 3 more accumulated
    byte_vec_t dec(257, 0x01);
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.prev_was_ff == 1);
    REQUIRE(ctx.code == 4);  // 3 bytes accumulated after 0xFF block
    REQUIRE(ctx.buf_len == 4);

    // end() should NOT take shortcut: code != 1, so goes to FLUSH_FINAL
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data() + inc_dst_len, 1, &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSH_FINAL);
  }

  SUBCASE("zero output in WRITE_DELIM makes no progress") {
    // Get to WRITE_DELIM: end() empty payload with room for code byte but not delimiter
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_WRITE_DELIM);

    // Now call with 0 output
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(dst_len == 0);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_WRITE_DELIM);
  }

  SUBCASE("zero output in FLUSHING makes no progress") {
    // Get to FLUSHING via inc with partial flush
    byte_vec_t dec{ 0x11, 0x00 };
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = 0;  // can't flush at all
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);

    // end() with 0 output should make no progress
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(!finished);
    REQUIRE(dst_len == 0);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);
  }

  SUBCASE("FLUSHING completes then prev_was_ff shortcut fires") {
    // Feed 254 nonzero, partially flush via inc
    byte_vec_t dec(254, 0x01);
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = dec.size();
    args.enc_dst_max = 200;  // not enough for full 255-byte block
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSHING);
    REQUIRE(ctx.prev_was_ff == 1);

    // end() with large output: FLUSHING → ACCUMULATE (prev_was_ff shortcut) → WRITE_DELIM
    // → DONE
    REQUIRE(cobs_encode_inc_end(&ctx,
                                enc_buf.data() + inc_dst_len,
                                enc_buf.size(),
                                &dst_len,
                                &finished) == COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_DONE);
    // Output = remaining flush bytes + delimiter (no trailing code byte)
    REQUIRE(enc_buf[inc_dst_len + dst_len - 1] == 0x00);
  }

  SUBCASE("resumed from FLUSH_FINAL completes") {
    // Feed a byte, end() with 0 output → stuck in FLUSH_FINAL
    byte_vec_t dec{ 0x42 };
    size_t src_len{}, inc_dst_len{};
    cobs_encode_inc_args_t args{};
    args.dec_src = dec.data();
    args.enc_dst = enc_buf.data();
    args.dec_src_max = 1;
    args.enc_dst_max = enc_buf.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &inc_dst_len) == COBS_RET_SUCCESS);

    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 0, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_FLUSH_FINAL);

    // Resume with enough space
    REQUIRE(
        cobs_encode_inc_end(&ctx, enc_buf.data(), enc_buf.size(), &dst_len, &finished) ==
        COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 3);  // code + data + delimiter
    REQUIRE(enc_buf[0] == 0x02);
    REQUIRE(enc_buf[1] == 0x42);
    REQUIRE(enc_buf[2] == 0x00);
  }

  SUBCASE("resumed from WRITE_DELIM completes") {
    // Get to WRITE_DELIM
    REQUIRE(cobs_encode_inc_end(&ctx, enc_buf.data(), 1, &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(ctx.state == cobs_enc_ctx_t::COBS_ENCODE_WRITE_DELIM);
    REQUIRE(enc_buf[0] == 0x01);

    // Resume — should write just the delimiter
    REQUIRE(cobs_encode_inc_end(&ctx,
                                enc_buf.data() + 1,
                                enc_buf.size(),
                                &dst_len,
                                &finished) == COBS_RET_SUCCESS);
    REQUIRE(finished);
    REQUIRE(dst_len == 1);
    REQUIRE(enc_buf[1] == 0x00);
  }
}

namespace {
byte_vec_t encode_single(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t enc_len{ 0u };
  byte_t dummy{ 0 };
  void const* src_ptr = dec.empty() ? &dummy : dec.data();
  REQUIRE(cobs_encode(src_ptr, dec.size(), enc.data(), enc.size(), &enc_len) ==
          COBS_RET_SUCCESS);
  enc.resize(enc_len);
  return enc;
}

byte_vec_t encode_incremental(byte_vec_t const& decoded,
                              size_t src_chunk,
                              size_t dst_chunk) {
  byte_vec_t work_buf(255);
  cobs_enc_ctx_t ctx{};
  REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
          COBS_RET_SUCCESS);

  byte_vec_t result;
  size_t src_pos = 0;

  while (src_pos < decoded.size()) {
    size_t const chunk = std::min(src_chunk, decoded.size() - src_pos);
    byte_vec_t dst(dst_chunk);

    cobs_encode_inc_args_t args{};
    args.dec_src = decoded.data() + src_pos;
    args.enc_dst = dst.data();
    args.dec_src_max = chunk;
    args.enc_dst_max = dst_chunk;

    size_t src_consumed{}, dst_written{};
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_consumed, &dst_written) == COBS_RET_SUCCESS);
    result.insert(result.end(), dst.data(), dst.data() + dst_written);
    src_pos += src_consumed;

    // If not all source was consumed, feed remaining with fresh output buffer
    while (src_consumed < chunk) {
      size_t remaining = chunk - src_consumed;
      byte_vec_t dst2(dst_chunk);
      cobs_encode_inc_args_t args2{};
      args2.dec_src = decoded.data() + src_pos;
      args2.enc_dst = dst2.data();
      args2.dec_src_max = remaining;
      args2.enc_dst_max = dst_chunk;

      size_t sc2{}, dw2{};
      REQUIRE(cobs_encode_inc(&ctx, &args2, &sc2, &dw2) == COBS_RET_SUCCESS);
      result.insert(result.end(), dst2.data(), dst2.data() + dw2);
      src_pos += sc2;
      src_consumed += sc2;
    }
  }

  // end
  bool finished = false;
  while (!finished) {
    byte_vec_t dst(dst_chunk);
    size_t dst_written{};
    REQUIRE(cobs_encode_inc_end(&ctx, dst.data(), dst_chunk, &dst_written, &finished) ==
            COBS_RET_SUCCESS);
    result.insert(result.end(), dst.data(), dst.data() + dst_written);
  }

  return result;
}

void verify_equivalence_all_chunks(byte_vec_t const& dec) {
  byte_vec_t const single = encode_single(dec);
  for (size_t src_chunk : { size_t{ 1 },
                            size_t{ 2 },
                            size_t{ 3 },
                            size_t{ 11 },
                            size_t{ 127 },
                            size_t{ 253 },
                            size_t{ 254 },
                            size_t{ 255 },
                            size_t{ dec.size() + 1 } }) {
    for (size_t dst_chunk : { size_t{ 1 },
                              size_t{ 2 },
                              size_t{ 3 },
                              size_t{ 127 },
                              size_t{ 255 },
                              size_t{ 256 },
                              size_t{ 1024 } }) {
      REQUIRE(encode_incremental(dec, src_chunk, dst_chunk) == single);
    }
  }
}
}  // namespace

TEST_CASE("Single/multi-encode equivalences") {
  SUBCASE("Empty payload") {
    verify_equivalence_all_chunks({});
  }

  SUBCASE("Single nonzero byte") {
    verify_equivalence_all_chunks({ 0x42 });
  }

  SUBCASE("Single zero byte") {
    verify_equivalence_all_chunks({ 0x00 });
  }

  SUBCASE("Small payloads") {
    verify_equivalence_all_chunks({ 0x11, 0x22 });
    verify_equivalence_all_chunks({ 0x00, 0x00 });
    verify_equivalence_all_chunks({ 0x11, 0x00, 0x22 });
    verify_equivalence_all_chunks({ 0x00, 0x11, 0x00 });
  }

  SUBCASE("Ascending bytes 1500") {
    byte_vec_t dec(1500);
    for (auto i{ 0u }; i < 1500; ++i) {
      dec[i] = i & 0xFF;
    }
    verify_equivalence_all_chunks(dec);
  }

  SUBCASE("All zero payload") {
    verify_equivalence_all_chunks(byte_vec_t(500, 0x00));
  }

  SUBCASE("All 0xFF payload") {
    verify_equivalence_all_chunks(byte_vec_t(500, 0xFF));
  }

  SUBCASE("253 nonzero bytes") {
    verify_equivalence_all_chunks(byte_vec_t(253, 0x01));
  }

  SUBCASE("254 nonzero bytes (0xFF boundary)") {
    verify_equivalence_all_chunks(byte_vec_t(254, 0x01));
  }

  SUBCASE("255 nonzero bytes (0xFF + 1)") {
    verify_equivalence_all_chunks(byte_vec_t(255, 0x01));
  }

  SUBCASE("508 nonzero bytes (two 0xFF blocks)") {
    verify_equivalence_all_chunks(byte_vec_t(508, 0xAA));
  }

  SUBCASE("509 nonzero bytes") {
    verify_equivalence_all_chunks(byte_vec_t(509, 0xAA));
  }

  SUBCASE("Zeros at 0xFF block boundaries") {
    byte_vec_t dec(600, 0x42);
    dec[253] = 0x00;
    dec[507] = 0x00;
    verify_equivalence_all_chunks(dec);
  }

  SUBCASE("Zero immediately after 0xFF boundary") {
    byte_vec_t dec(300, 0x01);
    dec[254] = 0x00;
    verify_equivalence_all_chunks(dec);
  }

  SUBCASE("Header / payload split") {
    byte_vec_t work_buf(255);
    cobs_enc_ctx_t ctx{};
    byte_vec_t enc_result;

    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
            COBS_RET_SUCCESS);

    byte_t const h[]{ 0x02, 0x03, 0xCC, 0xDF, 0x13, 0x49 };

    byte_vec_t dec_buf(400);
    std::iota(std::begin(dec_buf), std::end(dec_buf), byte_t{ 0x01 });
    dec_buf[4] = 0x00;
    dec_buf[27] = 0x00;
    dec_buf[45] = 0x00;
    dec_buf[68] = 0x00;

    // Encode header
    byte_vec_t dst(1024);
    cobs_encode_inc_args_t args{};
    args.dec_src = h;
    args.enc_dst = dst.data();
    args.dec_src_max = sizeof(h);
    args.enc_dst_max = dst.size();
    size_t src_len{}, dst_len{};
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    enc_result.insert(enc_result.end(), dst.data(), dst.data() + dst_len);

    // Encode payload
    args.dec_src = dec_buf.data();
    args.enc_dst = dst.data();
    args.dec_src_max = dec_buf.size();
    args.enc_dst_max = dst.size();
    REQUIRE(cobs_encode_inc(&ctx, &args, &src_len, &dst_len) == COBS_RET_SUCCESS);
    enc_result.insert(enc_result.end(), dst.data(), dst.data() + dst_len);

    // End
    bool finished{};
    REQUIRE(cobs_encode_inc_end(&ctx, dst.data(), dst.size(), &dst_len, &finished) ==
            COBS_RET_SUCCESS);
    REQUIRE(finished);
    enc_result.insert(enc_result.end(), dst.data(), dst.data() + dst_len);

    byte_vec_t single_dec(h, h + sizeof(h));
    single_dec.insert(std::end(single_dec),
                      dec_buf.data(),
                      dec_buf.data() + dec_buf.size());
    REQUIRE(enc_result == encode_single(single_dec));
  }
}

TEST_CASE("Small buffer edge cases") {
  SUBCASE("1-byte output buffer encodes correctly") {
    byte_vec_t payload{ 0x11, 0x22, 0x33, 0x00, 0x44 };
    REQUIRE(encode_incremental(payload, 1, 1) == encode_single(payload));
  }

  SUBCASE("Output buffer smaller than one block") {
    byte_vec_t payload(100, 0x42);
    REQUIRE(encode_incremental(payload, 100, 3) == encode_single(payload));
  }

  SUBCASE("Random chunk sizes") {
    std::mt19937 mt{ 42u };
    for (int iter = 0; iter < 100; ++iter) {
      size_t const len = 1 + (mt() % 1024);
      byte_vec_t payload(len);
      std::generate(payload.begin(), payload.end(), [&]() { return byte_t(mt()); });

      size_t const src_chunk = 1 + (mt() % 300);
      size_t const dst_chunk = 1 + (mt() % 300);
      REQUIRE(encode_incremental(payload, src_chunk, dst_chunk) == encode_single(payload));
    }
  }
}
