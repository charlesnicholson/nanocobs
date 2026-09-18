#include "../cobs.h"
#include "byte_vec.h"
#include "cobs_ref.h"
#include "cobs_shapes.h"
#include "doctest_wrapper.h"

#include <cstring>
#include <string>

// These attack the chunk boundary itself: every possible single split point, then
// ctx cloning across one.

namespace {

using cobs_test::make_shape;
using cobs_test::shape;

byte_vec_t oneshot_encode(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t n = 0;
  byte_t dummy = 0;
  REQUIRE(cobs_encode(dec.empty() ? &dummy : dec.data(),
                      dec.size(),
                      enc.data(),
                      enc.size(),
                      &n) == COBS_RET_SUCCESS);
  enc.resize(n);
  return enc;
}

// Drive cobs_encode_inc with the source delivered as exactly two chunks split at
// |split|, and a deliberately awkward destination chunk size.
byte_vec_t encode_inc_split(byte_vec_t const& dec, size_t split, size_t dst_chunk) {
  byte_t work[255];
  cobs_enc_ctx_t ctx;
  REQUIRE(cobs_encode_inc_begin(&ctx, work, sizeof(work)) == COBS_RET_SUCCESS);

  byte_vec_t out, chunk(dst_chunk);
  size_t bounds[3] = { 0, split, dec.size() };
  for (size_t part = 0; part < 2; ++part) {
    size_t at = bounds[part];
    size_t const end = bounds[part + 1];
    while (at < end) {
      size_t su = 0, du = 0;
      cobs_encode_inc_args_t const args{ dec.data() + at,
                                         chunk.data(),
                                         end - at,
                                         dst_chunk };
      REQUIRE(cobs_encode_inc(&ctx, &args, &su, &du) == COBS_RET_SUCCESS);
      REQUIRE(su <= (end - at));
      REQUIRE(du <= dst_chunk);
      out.insert(out.end(), chunk.data(), chunk.data() + du);
      if (!su && !du) {
        break;
      }
      at += su;
    }
  }

  bool finished = false;
  while (!finished) {
    size_t du = 0;
    REQUIRE(cobs_encode_inc_end(&ctx, chunk.data(), dst_chunk, &du, &finished) ==
            COBS_RET_SUCCESS);
    out.insert(out.end(), chunk.data(), chunk.data() + du);
    if (!du && !finished) {
      FAIL("cobs_encode_inc_end made no progress");
    }
  }
  return out;
}

byte_vec_t decode_inc_split(byte_vec_t const& enc, size_t split, size_t dst_chunk) {
  cobs_decode_inc_ctx_t ctx;
  REQUIRE(cobs_decode_inc_begin(&ctx) == COBS_RET_SUCCESS);
  byte_vec_t out, chunk(dst_chunk);
  size_t bounds[3] = { 0, split, enc.size() };
  bool complete = false;
  for (size_t part = 0; (part < 2) && !complete; ++part) {
    size_t at = bounds[part];
    size_t const end = bounds[part + 1];
    while ((at < end) && !complete) {
      size_t su = 0, du = 0;
      cobs_decode_inc_args_t const args{ enc.data() + at,
                                         chunk.data(),
                                         end - at,
                                         dst_chunk };
      REQUIRE(cobs_decode_inc(&ctx, &args, &su, &du, &complete) == COBS_RET_SUCCESS);
      out.insert(out.end(), chunk.data(), chunk.data() + du);
      if (!su && !du) {
        break;
      }
      at += su;
    }
  }
  REQUIRE(complete);
  return out;
}

size_t const kDstChunks[] = { 1, 2, 3, 7, 8, 9, 16, 17, 64, 1024 };

}  // namespace

TEST_CASE("SWAR boundaries: incremental encode at every single split point") {
  size_t const lens[] = { 0, 1, 7, 8, 9, 15, 16, 17, 33, 253, 254, 255, 256, 509 };
  for (size_t const len : lens) {
    for (shape const sh : { shape::nonzero,
                            shape::bernoulli_5,
                            shape::all_zero,
                            shape::zero_every_8,
                            shape::zero_every_9 }) {
      byte_vec_t const dec = make_shape(sh, len, 0x51D3u);
      byte_vec_t const expect = oneshot_encode(dec);
      for (size_t split = 0; split <= len; ++split) {
        for (size_t const dc : kDstChunks) {
          byte_vec_t const got = encode_inc_split(dec, split, dc);
          if (got != expect) {
            FAIL("encode_inc split=" << split << " dst_chunk=" << dc << " len=" << len
                                     << " shape=" << cobs_test::shape_name(sh));
          }
        }
      }
    }
  }
}

TEST_CASE("SWAR boundaries: incremental decode at every single split point") {
  size_t const lens[] = { 0, 1, 7, 8, 9, 16, 17, 33, 253, 254, 255, 256, 509 };
  for (size_t const len : lens) {
    for (shape const sh : { shape::nonzero,
                            shape::bernoulli_5,
                            shape::all_zero,
                            shape::zero_every_8,
                            shape::zero_every_9 }) {
      byte_vec_t const dec = make_shape(sh, len, 0x7A11u);
      byte_vec_t const enc = oneshot_encode(dec);
      for (size_t split = 0; split <= enc.size(); ++split) {
        for (size_t const dc : kDstChunks) {
          byte_vec_t const got = decode_inc_split(enc, split, dc);
          if (got != dec) {
            FAIL("decode_inc split=" << split << " dst_chunk=" << dc << " len=" << len
                                     << " shape=" << cobs_test::shape_name(sh));
          }
        }
      }
    }
  }
}

TEST_CASE("SWAR boundaries: encode context is clonable mid-stream") {
  // A fast lane that stashed state outside cobs_enc_ctx_t would diverge when the
  // stream is finished from a byte copy of the ctx.
  size_t const lens[] = { 8, 9, 16, 17, 64, 254, 255, 300 };
  for (size_t const len : lens) {
    for (shape const sh : { shape::nonzero, shape::bernoulli_5, shape::zero_every_9 }) {
      byte_vec_t const dec = make_shape(sh, len, 0xC10Eu);
      byte_vec_t const expect = oneshot_encode(dec);
      for (size_t split = 0; split <= len; ++split) {
        byte_t work[255];
        cobs_enc_ctx_t ctx;
        REQUIRE(cobs_encode_inc_begin(&ctx, work, sizeof(work)) == COBS_RET_SUCCESS);
        byte_vec_t out, chunk(64);
        size_t at = 0;
        while (at < split) {
          size_t su = 0, du = 0;
          cobs_encode_inc_args_t const args{ dec.data() + at,
                                             chunk.data(),
                                             split - at,
                                             chunk.size() };
          REQUIRE(cobs_encode_inc(&ctx, &args, &su, &du) == COBS_RET_SUCCESS);
          out.insert(out.end(), chunk.data(), chunk.data() + du);
          if (!su && !du) {
            break;
          }
          at += su;
        }

        // Clone the ctx through opaque bytes and finish from the copy. The work
        // buffer is shared on purpose: it is reachable only through ctx->buf.
        byte_t ctx_bytes[sizeof(cobs_enc_ctx_t)];
        std::memcpy(ctx_bytes, &ctx, sizeof(ctx));
        cobs_enc_ctx_t clone;
        std::memcpy(&clone, ctx_bytes, sizeof(clone));

        while (at < len) {
          size_t su = 0, du = 0;
          cobs_encode_inc_args_t const args{ dec.data() + at,
                                             chunk.data(),
                                             len - at,
                                             chunk.size() };
          REQUIRE(cobs_encode_inc(&clone, &args, &su, &du) == COBS_RET_SUCCESS);
          out.insert(out.end(), chunk.data(), chunk.data() + du);
          if (!su && !du) {
            break;
          }
          at += su;
        }
        bool finished = false;
        while (!finished) {
          size_t du = 0;
          REQUIRE(
              cobs_encode_inc_end(&clone, chunk.data(), chunk.size(), &du, &finished) ==
              COBS_RET_SUCCESS);
          out.insert(out.end(), chunk.data(), chunk.data() + du);
        }
        if (out != expect) {
          FAIL("clone split=" << split << " len=" << len
                              << " shape=" << cobs_test::shape_name(sh));
        }
      }
    }
  }
}

TEST_CASE("SWAR boundaries: zero-capacity calls make no progress and corrupt nothing") {
  byte_vec_t const dec = make_shape(shape::bernoulli_5, 600, 0xBEEFu);
  byte_vec_t const expect = oneshot_encode(dec);

  byte_t work[255];
  cobs_enc_ctx_t ctx;
  REQUIRE(cobs_encode_inc_begin(&ctx, work, sizeof(work)) == COBS_RET_SUCCESS);
  byte_vec_t out, chunk(32);
  size_t at = 0;
  unsigned tick = 0;
  while (at < dec.size()) {
    // Interleave calls that offer no source and calls that offer no destination.
    size_t const src_avail = ((tick % 3u) == 1u) ? 0u : (dec.size() - at);
    size_t const dst_avail = ((tick % 3u) == 2u) ? 0u : chunk.size();
    size_t su = 0, du = 0;
    cobs_encode_inc_args_t const args{ dec.data() + at,
                                       chunk.data(),
                                       src_avail,
                                       dst_avail };
    REQUIRE(cobs_encode_inc(&ctx, &args, &su, &du) == COBS_RET_SUCCESS);
    REQUIRE(su <= src_avail);
    REQUIRE(du <= dst_avail);
    if (!src_avail) {
      REQUIRE(su == 0);
    }
    if (!dst_avail) {
      REQUIRE(du == 0);
    }
    out.insert(out.end(), chunk.data(), chunk.data() + du);
    at += su;
    ++tick;
    REQUIRE(tick < 100000u);
  }
  bool finished = false;
  while (!finished) {
    size_t du = 0;
    REQUIRE(cobs_encode_inc_end(&ctx, chunk.data(), chunk.size(), &du, &finished) ==
            COBS_RET_SUCCESS);
    out.insert(out.end(), chunk.data(), chunk.data() + du);
  }
  REQUIRE(out == expect);
}

TEST_CASE("SWAR boundaries: round trip at every length 0..600 across every shape") {
  size_t n_shapes = 0;
  shape const* shapes = cobs_test::all_shapes(&n_shapes);
  for (size_t si = 0; si < n_shapes; ++si) {
    for (size_t len = 0; len <= 600; ++len) {
      byte_vec_t const dec = make_shape(shapes[si], len, static_cast<uint32_t>(len) + 7u);
      byte_vec_t const enc = oneshot_encode(dec);
      REQUIRE(enc.size() >= 2);
      REQUIRE(enc.back() == 0x00);
      for (size_t i = 0; i + 1 < enc.size(); ++i) {
        REQUIRE(enc[i] != 0x00);
      }
      byte_vec_t back(len + 2);
      size_t got = 0, consumed = 0;
      REQUIRE(cobs_decode(enc.data(), enc.size(), back.data(), back.size(), &got,
                          &consumed) == COBS_RET_SUCCESS);
      REQUIRE(consumed == enc.size());
      back.resize(got);
      if (back != dec) {
        FAIL("round trip len=" << len << " shape=" << cobs_test::shape_name(shapes[si]));
      }
    }
  }
}
