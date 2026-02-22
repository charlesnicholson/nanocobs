#include "../cobs.h"
#include "byte_vec.h"

#include "doctest_wrapper.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <random>
#include <thread>

using std::atomic;
using std::generate;
using std::max;
using std::min;
using std::mt19937;
using std::none_of;
using std::thread;
using std::vector;

auto constexpr LEN{ 4u * 1024u * 1024u };

namespace {
atomic<int> s_iterations{ 250u };
}

TEST_CASE("many random payloads") {
  auto const thread_proc{ [](unsigned seed) {
    mt19937 mt{ seed };  // deterministic
    byte_vec_t src(LEN), dec(LEN), enc(COBS_ENCODE_MAX(LEN));

    while (--s_iterations > 0) {
      generate(src.begin(), src.end(), [&]() { return byte_t(mt()); });
      auto const run_ofs = mt() % (LEN - 2560);
      memset(src.data() + run_ofs, 0xAA, 256 * 10);  // nonzero run

      size_t enc_len{ 0u };
      REQUIRE(cobs_encode(src.data(), src.size(), enc.data(), enc.size(), &enc_len) ==
              COBS_RET_SUCCESS);

      REQUIRE(enc_len >= LEN);
      REQUIRE(enc_len <= COBS_ENCODE_MAX(LEN));
      REQUIRE(none_of(enc.data(), enc.data() + enc_len - 1, [](byte_t b) { return !b; }));
      REQUIRE(enc[enc_len - 1] == 0);

      size_t dec_len{ 0u };
      REQUIRE(cobs_decode(enc.data(), enc_len, dec.data(), dec.size(), &dec_len) ==
              COBS_RET_SUCCESS);

      REQUIRE(dec_len == LEN);
      REQUIRE(src == dec);
    }
  } };

  vector<thread> threads;
  for (auto i{ 0u }, n{ max(1u, thread::hardware_concurrency() - 1) }; i < n; ++i) {
    threads.emplace_back(thread_proc, i);
  }

  for (auto& t : threads) {
    t.join();
  }
}

TEST_CASE("random payloads near code-block boundaries") {
  mt19937 mt{ 12345u };

  for (auto iter{ 0u }; iter < 500; ++iter) {
    // Sizes near 254-byte boundaries where 0xFF code blocks matter
    size_t const sizes[] = { 1, 2, 127, 253, 254, 255, 256, 507, 508, 509, 512, 1024 };
    size_t const len = sizes[mt() % (sizeof(sizes) / sizeof(sizes[0]))];

    byte_vec_t src(len);
    generate(src.begin(), src.end(), [&]() { return byte_t(mt()); });

    byte_vec_t enc(COBS_ENCODE_MAX(len));
    size_t enc_len{ 0u };
    REQUIRE(cobs_encode(src.data(), src.size(), enc.data(), enc.size(), &enc_len) ==
            COBS_RET_SUCCESS);

    byte_vec_t dec(len);
    size_t dec_len{ 0u };
    REQUIRE(cobs_decode(enc.data(), enc_len, dec.data(), dec.size(), &dec_len) ==
            COBS_RET_SUCCESS);
    REQUIRE(dec_len == len);
    REQUIRE(src == dec);
  }
}

TEST_CASE("random incremental encode vs single-shot") {
  mt19937 mt{ 67890u };

  for (auto iter{ 0u }; iter < 200; ++iter) {
    size_t const len = 1 + (mt() % 2048);

    byte_vec_t src(len);
    generate(src.begin(), src.end(), [&]() { return byte_t(mt()); });

    // Single-shot encode
    byte_vec_t enc_single(COBS_ENCODE_MAX(len));
    size_t enc_single_len{ 0u };
    REQUIRE(cobs_encode(src.data(),
                        src.size(),
                        enc_single.data(),
                        enc_single.size(),
                        &enc_single_len) == COBS_RET_SUCCESS);

    // Incremental encode with random src AND dst chunk sizes
    byte_vec_t work_buf(255);
    cobs_enc_ctx_t ctx{};
    REQUIRE(cobs_encode_inc_begin(&ctx, work_buf.data(), work_buf.size()) ==
            COBS_RET_SUCCESS);

    byte_vec_t enc_inc;
    size_t src_pos{ 0 };

    while (src_pos < len) {
      size_t const src_chunk = min(size_t(1 + (mt() % 300)), len - src_pos);
      size_t const dst_chunk = 1 + (mt() % 512);
      byte_vec_t dst(dst_chunk);

      cobs_encode_inc_args_t args{};
      args.dec_src = src.data() + src_pos;
      args.enc_dst = dst.data();
      args.dec_src_max = src_chunk;
      args.enc_dst_max = dst_chunk;

      size_t src_consumed{}, dst_written{};
      REQUIRE(cobs_encode_inc(&ctx, &args, &src_consumed, &dst_written) ==
              COBS_RET_SUCCESS);
      enc_inc.insert(enc_inc.end(), dst.data(), dst.data() + dst_written);
      src_pos += src_consumed;

      // If not all source consumed, keep feeding remaining
      size_t chunk_remaining = src_chunk - src_consumed;
      while (chunk_remaining > 0) {
        size_t const dc2 = 1 + (mt() % 512);
        byte_vec_t dst2(dc2);
        cobs_encode_inc_args_t args2{};
        args2.dec_src = src.data() + src_pos;
        args2.enc_dst = dst2.data();
        args2.dec_src_max = chunk_remaining;
        args2.enc_dst_max = dc2;

        size_t sc2{}, dw2{};
        REQUIRE(cobs_encode_inc(&ctx, &args2, &sc2, &dw2) == COBS_RET_SUCCESS);
        enc_inc.insert(enc_inc.end(), dst2.data(), dst2.data() + dw2);
        src_pos += sc2;
        chunk_remaining -= sc2;
      }
    }

    // Finish
    bool finished{ false };
    while (!finished) {
      size_t const dc = 1 + (mt() % 64);
      byte_vec_t dst(dc);
      size_t dw{};
      REQUIRE(cobs_encode_inc_end(&ctx, dst.data(), dc, &dw, &finished) ==
              COBS_RET_SUCCESS);
      enc_inc.insert(enc_inc.end(), dst.data(), dst.data() + dw);
    }

    REQUIRE(enc_inc.size() == enc_single_len);
    REQUIRE(enc_inc == byte_vec_t(enc_single.data(), enc_single.data() + enc_single_len));

    // Verify round-trip decode
    byte_vec_t dec(len);
    size_t dec_len{ 0u };
    REQUIRE(
        cobs_decode(enc_inc.data(), enc_inc.size(), dec.data(), dec.size(), &dec_len) ==
        COBS_RET_SUCCESS);
    REQUIRE(dec_len == len);
    REQUIRE(dec == src);
  }
}

TEST_CASE("random incremental decode vs single-shot") {
  mt19937 mt{ 11111u };

  for (auto iter{ 0u }; iter < 200; ++iter) {
    size_t const len = 1 + (mt() % 2048);

    byte_vec_t src(len);
    generate(src.begin(), src.end(), [&]() { return byte_t(mt()); });

    byte_vec_t enc(COBS_ENCODE_MAX(len));
    size_t enc_len{ 0u };
    REQUIRE(cobs_encode(src.data(), src.size(), enc.data(), enc.size(), &enc_len) ==
            COBS_RET_SUCCESS);

    // Incremental decode with random chunk sizes
    cobs_decode_inc_ctx_t ctx;
    REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);

    byte_vec_t dec(len);
    size_t src_pos{ 0 }, dst_pos{ 0 };
    bool complete{ false };

    while (!complete && src_pos < enc_len) {
      size_t const chunk = min(size_t(1 + (mt() % 300)), enc_len - src_pos);

      size_t src_consumed{ 0u }, dst_written{ 0u };
      cobs_decode_inc_args_t args;
      args.enc_src = enc.data() + src_pos;
      args.dec_dst = dec.data() + dst_pos;
      args.enc_src_max = chunk;
      args.dec_dst_max = len - dst_pos;

      REQUIRE(cobs_decode_inc(&ctx, &args, &src_consumed, &dst_written, &complete) ==
              COBS_RET_SUCCESS);
      src_pos += src_consumed;
      dst_pos += dst_written;
    }

    REQUIRE(complete);
    REQUIRE(dst_pos == len);
    REQUIRE(dec == src);
  }
}

TEST_CASE("random tinyframe round-trips") {
  mt19937 mt{ 99999u };

  for (auto iter{ 0u }; iter < 500; ++iter) {
    // Tinyframe safe buffer is 256; test sizes within that range
    size_t const payload_len = mt() % (COBS_TINYFRAME_SAFE_BUFFER_SIZE - 2);
    size_t const buf_len = payload_len + 2;

    byte_vec_t buf(buf_len);
    buf[0] = COBS_TINYFRAME_SENTINEL_VALUE;
    buf[buf_len - 1] = COBS_TINYFRAME_SENTINEL_VALUE;
    for (size_t i{ 1 }; i <= payload_len; ++i) {
      buf[i] = byte_t(mt());
    }

    byte_vec_t original(buf.begin() + 1, buf.begin() + 1 + static_cast<long>(payload_len));

    REQUIRE(cobs_encode_tinyframe(buf.data(), buf_len) == COBS_RET_SUCCESS);

    // Encoded frame must end with 0x00 and contain no interior zeros
    REQUIRE(buf[buf_len - 1] == 0x00);
    REQUIRE(none_of(buf.data(), buf.data() + buf_len - 1, [](byte_t b) { return !b; }));

    REQUIRE(cobs_decode_tinyframe(buf.data(), buf_len) == COBS_RET_SUCCESS);

    // Sentinels restored
    REQUIRE(buf[0] == COBS_TINYFRAME_SENTINEL_VALUE);
    REQUIRE(buf[buf_len - 1] == COBS_TINYFRAME_SENTINEL_VALUE);

    // Payload matches original
    REQUIRE(byte_vec_t(buf.begin() + 1,
                       buf.begin() + 1 + static_cast<long>(payload_len)) == original);
  }
}
