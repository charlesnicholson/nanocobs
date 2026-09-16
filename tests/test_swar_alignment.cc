#include "../cobs.h"
#include "byte_vec.h"
#include "cobs_ref.h"
#include "cobs_shapes.h"
#include "doctest_wrapper.h"
#include "guarded_buf.h"

#include <cstring>
#include <string>

// std::vector data is always 16-byte aligned, so only guard-page buffers at an
// explicit offset exercise the unaligned word path.

namespace {

using cobs_test::guarded_buf;
using cobs_test::make_shape;
using cobs_test::shape;

byte_t constexpr kPoison = 0xCD;

byte_vec_t ref_encode(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t n = 0;
  byte_t dummy = 0;
  REQUIRE(cobs_ref_encode(dec.empty() ? &dummy : dec.data(),
                          dec.size(),
                          enc.data(),
                          enc.size(),
                          &n) == COBS_RET_SUCCESS);
  enc.resize(n);
  return enc;
}

shape const kCorpus[] = {
  shape::nonzero,      shape::all_zero,     shape::bernoulli_5,    shape::bernoulli_50,
  shape::zero_every_8, shape::zero_every_9, shape::zero_every_254,
};

size_t const kLens[] = { 0, 1, 7, 8, 9, 15, 16, 17, 33, 254, 255, 300 };

}  // namespace

TEST_CASE("SWAR alignment: encode and decode at every src/dst offset pair") {
  for (shape const sh : kCorpus) {
    for (size_t const len : kLens) {
      byte_vec_t const payload = make_shape(sh, len, 0x5EEDu);
      byte_vec_t const expect_enc = ref_encode(payload);

      for (size_t src_ofs = 0; src_ofs < 16; ++src_ofs) {
        for (size_t dst_ofs = 0; dst_ofs < 16; ++dst_ofs) {
          guarded_buf src(len ? len : 1, src_ofs);
          if (len) {
            src.assign(payload.data(), len);
          }
          guarded_buf enc(expect_enc.size(), dst_ofs);
          enc.fill(kPoison);

          size_t enc_len = 0;
          REQUIRE(cobs_encode(src.data(), len, enc.data(), enc.size(), &enc_len) ==
                  COBS_RET_SUCCESS);
          REQUIRE(enc_len == expect_enc.size());
          REQUIRE(std::memcmp(enc.data(), expect_enc.data(), enc_len) == 0);

          guarded_buf dec(len ? len : 1, (dst_ofs + 5u) % 16u);
          dec.fill(kPoison);
          size_t dec_len = 0;
          REQUIRE(cobs_decode(enc.data(), enc_len, dec.data(), dec.size(), &dec_len) ==
                  COBS_RET_SUCCESS);
          REQUIRE(dec_len == len);
          if (len) {
            REQUIRE(std::memcmp(dec.data(), payload.data(), len) == 0);
          }
        }
      }
    }
  }
}

TEST_CASE("SWAR alignment: tinyframe at every buffer offset and length") {
  for (size_t len = 2; len <= 258; ++len) {
    for (size_t ofs = 0; ofs < 16; ++ofs) {
      byte_vec_t payload = make_shape(shape::bernoulli_5, len - 2u, 0xC0FFEEu);
      byte_vec_t frame;
      frame.reserve(len);
      frame.push_back(COBS_TINYFRAME_SENTINEL_VALUE);
      frame.insert(frame.end(), payload.begin(), payload.end());
      frame.push_back(COBS_TINYFRAME_SENTINEL_VALUE);

      guarded_buf a(len, ofs), b(len, ofs);
      a.assign(frame.data(), len);
      b.assign(frame.data(), len);
      REQUIRE(cobs_ref_encode_tinyframe(a.data(), len) == COBS_RET_SUCCESS);
      REQUIRE(cobs_encode_tinyframe(b.data(), len) == COBS_RET_SUCCESS);
      REQUIRE(std::memcmp(a.data(), b.data(), len) == 0);

      REQUIRE(cobs_ref_decode_tinyframe(a.data(), len) == COBS_RET_SUCCESS);
      REQUIRE(cobs_decode_tinyframe(b.data(), len) == COBS_RET_SUCCESS);
      REQUIRE(std::memcmp(a.data(), b.data(), len) == 0);
      REQUIRE(std::memcmp(b.data(), frame.data(), len) == 0);
    }
  }
}

TEST_CASE("SWAR alignment: in-place decode with runs long enough to reach the fast lane") {
  // Long enough to actually enter a word-wide fast lane, at every alignment.
  size_t const lens[] = { 16, 17, 31, 32, 33, 64, 253, 254, 255, 256, 700 };
  for (size_t const len : lens) {
    for (shape const sh : { shape::nonzero, shape::bernoulli_5, shape::zero_every_254 }) {
      byte_vec_t const payload = make_shape(sh, len, 0xABCDu);
      byte_vec_t const enc = ref_encode(payload);
      for (size_t ofs = 0; ofs < 16; ++ofs) {
        guarded_buf buf(enc.size(), ofs);
        buf.assign(enc.data(), enc.size());
        size_t dec_len = 0;
        // out_dec == enc: a supported, documented aliasing configuration.
        REQUIRE(cobs_decode(buf.data(), enc.size(), buf.data(), enc.size(), &dec_len) ==
                COBS_RET_SUCCESS);
        REQUIRE(dec_len == len);
        REQUIRE(std::memcmp(buf.data(), payload.data(), len) == 0);
      }
    }
  }
}

TEST_CASE("SWAR alignment: incremental encode work buffer is exactly 255 bytes") {
  // cobs_encode_inc_begin only requires 255 bytes, so a word store past buf[254]
  // overflows the caller. Here that faults instead of landing in allocator slack.
  size_t const lens[] = { 0, 1, 253, 254, 255, 256, 507, 508, 509, 1000 };
  for (size_t const len : lens) {
    for (shape const sh : { shape::nonzero, shape::bernoulli_5, shape::all_zero }) {
      byte_vec_t const payload = make_shape(sh, len, 0x1234u);
      byte_vec_t const expect = ref_encode(payload);
      for (size_t ofs = 0; ofs < 16; ++ofs) {
        guarded_buf work(255, ofs);
        cobs_enc_ctx_t ctx;
        REQUIRE(cobs_encode_inc_begin(&ctx, work.data(), work.size()) == COBS_RET_SUCCESS);

        byte_vec_t out;
        byte_vec_t chunk(64);
        size_t consumed = 0;
        byte_t dummy = 0;
        while (consumed < len) {
          size_t const take = ((len - consumed) < 37u) ? (len - consumed) : 37u;
          size_t su = 0, du = 0;
          cobs_encode_inc_args_t const args{ payload.data() + consumed,
                                             chunk.data(),
                                             take,
                                             chunk.size() };
          REQUIRE(cobs_encode_inc(&ctx, &args, &su, &du) == COBS_RET_SUCCESS);
          out.insert(out.end(), chunk.data(), chunk.data() + du);
          consumed += su;
          if (!su && !du) {
            break;
          }
        }
        (void)dummy;
        bool finished = false;
        while (!finished) {
          size_t du = 0;
          REQUIRE(cobs_encode_inc_end(&ctx, chunk.data(), chunk.size(), &du, &finished) ==
                  COBS_RET_SUCCESS);
          out.insert(out.end(), chunk.data(), chunk.data() + du);
        }
        REQUIRE(out == expect);
      }
    }
  }
}
