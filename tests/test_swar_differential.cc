#include "../cobs.h"
#include "byte_vec.h"
#include "cobs_ref.h"
#include "doctest_wrapper.h"
#include "guarded_buf.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

// The shipping cobs.c against the same source built with -DCOBS_SWAR_WORD_BITS=8, so
// the byte loop is the oracle. Destinations are poisoned and compared in full.

namespace {

using cobs_test::guarded_buf;

byte_t constexpr kPoison = 0xCD;
size_t constexpr kSentinel = static_cast<size_t>(-1);

std::string hex(byte_t const* p, size_t n) {
  std::string s;
  char buf[8];
  size_t const shown = (n > 48) ? 48 : n;
  for (size_t i = 0; i < shown; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X ", p[i]);
    s += buf;
  }
  if (shown != n) {
    s += "...";
  }
  return s;
}

std::string first_diff(byte_t const* a, byte_t const* b, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      char buf[96];
      std::snprintf(buf,
                    sizeof(buf),
                    "first byte diff at [%zu]: ref=0x%02X swar=0x%02X",
                    i,
                    a[i],
                    b[i]);
      return buf;
    }
  }
  return {};
}

// ------------------------------------------------------------------ encode ---

std::string diff_encode(byte_t const* payload,
                        size_t len,
                        size_t enc_max,
                        size_t src_ofs,
                        size_t dst_ofs) {
  guarded_buf src(len ? len : 1, src_ofs);
  if (len) {
    src.assign(payload, len);
  }
  guarded_buf a(enc_max ? enc_max : 1, dst_ofs), b(enc_max ? enc_max : 1, dst_ofs);
  a.fill(kPoison);
  b.fill(kPoison);

  size_t la = kSentinel, lb = kSentinel;
  cobs_ret_t const ra = cobs_ref_encode(src.data(), len, a.data(), enc_max, &la);
  cobs_ret_t const rb = cobs_encode(src.data(), len, b.data(), enc_max, &lb);

  char msg[160];
  if (ra != rb) {
    std::snprintf(msg, sizeof(msg), "ret mismatch: ref=%d swar=%d", ra, rb);
    return msg;
  }
  if (la != lb) {
    std::snprintf(msg, sizeof(msg), "out_len mismatch: ref=%zu swar=%zu", la, lb);
    return msg;
  }
  if (enc_max && (std::memcmp(a.data(), b.data(), enc_max) != 0)) {
    return first_diff(a.data(), b.data(), enc_max);
  }
  return {};
}

// ------------------------------------------------------------------ decode ---

std::string diff_decode(byte_t const* enc,
                        size_t enc_len,
                        size_t dec_max,
                        size_t src_ofs,
                        size_t dst_ofs) {
  guarded_buf src(enc_len ? enc_len : 1, src_ofs);
  if (enc_len) {
    src.assign(enc, enc_len);
  }
  guarded_buf a(dec_max ? dec_max : 1, dst_ofs), b(dec_max ? dec_max : 1, dst_ofs);
  a.fill(kPoison);
  b.fill(kPoison);

  size_t la = kSentinel, lb = kSentinel;
  cobs_ret_t const ra = cobs_ref_decode(src.data(), enc_len, a.data(), dec_max, &la);
  cobs_ret_t const rb = cobs_decode(src.data(), enc_len, b.data(), dec_max, &lb);

  char msg[160];
  if (ra != rb) {
    std::snprintf(msg, sizeof(msg), "ret mismatch: ref=%d swar=%d", ra, rb);
    return msg;
  }
  if (la != lb) {
    std::snprintf(msg, sizeof(msg), "out_len mismatch: ref=%zu swar=%zu", la, lb);
    return msg;
  }
  if (dec_max && (std::memcmp(a.data(), b.data(), dec_max) != 0)) {
    return first_diff(a.data(), b.data(), dec_max);
  }
  return {};
}

// --------------------------------------------------------------- tinyframe ---

std::string diff_tinyframe(byte_t const* buf, size_t len, size_t ofs, bool encode) {
  guarded_buf a(len, ofs), b(len, ofs);
  a.assign(buf, len);
  b.assign(buf, len);

  cobs_ret_t const ra = encode ? cobs_ref_encode_tinyframe(a.data(), len)
                               : cobs_ref_decode_tinyframe(a.data(), len);
  cobs_ret_t const rb =
      encode ? cobs_encode_tinyframe(b.data(), len) : cobs_decode_tinyframe(b.data(), len);

  char msg[160];
  if (ra != rb) {
    std::snprintf(msg, sizeof(msg), "ret mismatch: ref=%d swar=%d", ra, rb);
    return msg;
  }
  // On BAD_PAYLOAD the docs leave the buffer indeterminate, but both builds run
  // the same algorithm, so they must still agree byte-for-byte.
  if (std::memcmp(a.data(), b.data(), len) != 0) {
    return first_diff(a.data(), b.data(), len);
  }
  return {};
}

// ------------------------------------------------------- incremental encode ---

struct inc_enc_result {
  byte_vec_t out;
  byte_vec_t work_tail;  // work buffer contents up to buf_len
  cobs_ret_t ret;
  unsigned state, code, buf_len, flush_pos, prev_was_ff;
  bool finished;
};

template <typename BeginFn, typename IncFn, typename EndFn>
inc_enc_result run_enc_inc(BeginFn begin,
                           IncFn inc,
                           EndFn end,
                           byte_t const* payload,
                           size_t len,
                           size_t src_chunk,
                           size_t dst_chunk,
                           size_t work_ofs) {
  inc_enc_result r{};
  // Exactly 255 bytes against a guard page: a word store that runs past buf[254]
  // faults here instead of quietly landing in allocator slack.
  guarded_buf work(255, work_ofs);
  work.fill(kPoison);

  cobs_enc_ctx_t ctx;
  r.ret = begin(&ctx, work.data(), work.size());
  if (r.ret != COBS_RET_SUCCESS) {
    return r;
  }

  byte_vec_t chunk(dst_chunk);
  size_t consumed = 0;
  while (consumed < len) {
    size_t const take = ((len - consumed) < src_chunk) ? (len - consumed) : src_chunk;
    size_t src_used = 0, dst_used = 0;
    cobs_encode_inc_args_t const args{ payload + consumed, chunk.data(), take, dst_chunk };
    r.ret = inc(&ctx, &args, &src_used, &dst_used);
    if (r.ret != COBS_RET_SUCCESS) {
      return r;
    }
    REQUIRE(src_used <= take);
    REQUIRE(dst_used <= dst_chunk);
    r.out.insert(r.out.end(), chunk.data(), chunk.data() + dst_used);
    if (!src_used && !dst_used) {
      break;  // no forward progress possible
    }
    consumed += src_used;
  }

  r.finished = false;
  for (int guard = 0; !r.finished && (guard < 4096); ++guard) {
    size_t dst_used = 0;
    r.ret = end(&ctx, chunk.data(), dst_chunk, &dst_used, &r.finished);
    if (r.ret != COBS_RET_SUCCESS) {
      return r;
    }
    r.out.insert(r.out.end(), chunk.data(), chunk.data() + dst_used);
    if (!dst_used && !r.finished) {
      break;
    }
  }

  r.state = static_cast<unsigned>(ctx.state);
  r.code = ctx.code;
  r.buf_len = ctx.buf_len;
  r.flush_pos = ctx.flush_pos;
  r.prev_was_ff = ctx.prev_was_ff;
  r.work_tail.assign(work.data(), work.data() + ctx.buf_len);
  return r;
}

std::string diff_encode_inc(byte_t const* payload,
                            size_t len,
                            size_t src_chunk,
                            size_t dst_chunk,
                            size_t work_ofs) {
  inc_enc_result const a = run_enc_inc(cobs_ref_encode_inc_begin,
                                       cobs_ref_encode_inc,
                                       cobs_ref_encode_inc_end,
                                       payload,
                                       len,
                                       src_chunk,
                                       dst_chunk,
                                       work_ofs);
  inc_enc_result const b = run_enc_inc(cobs_encode_inc_begin,
                                       cobs_encode_inc,
                                       cobs_encode_inc_end,
                                       payload,
                                       len,
                                       src_chunk,
                                       dst_chunk,
                                       work_ofs);
  char msg[192];
  if (a.ret != b.ret) {
    std::snprintf(msg, sizeof(msg), "ret mismatch: ref=%d swar=%d", a.ret, b.ret);
    return msg;
  }
  if (a.finished != b.finished) {
    return "finished mismatch";
  }
  if (a.out.size() != b.out.size()) {
    std::snprintf(msg,
                  sizeof(msg),
                  "enc len mismatch: ref=%zu swar=%zu",
                  a.out.size(),
                  b.out.size());
    return msg;
  }
  if (!a.out.empty() && (std::memcmp(a.out.data(), b.out.data(), a.out.size()) != 0)) {
    return first_diff(a.out.data(), b.out.data(), a.out.size());
  }
  if ((a.state != b.state) || (a.code != b.code) || (a.buf_len != b.buf_len) ||
      (a.flush_pos != b.flush_pos) || (a.prev_was_ff != b.prev_was_ff)) {
    std::snprintf(msg,
                  sizeof(msg),
                  "ctx mismatch: ref{%u,%u,%u,%u,%u} swar{%u,%u,%u,%u,%u}",
                  a.state,
                  a.code,
                  a.buf_len,
                  a.flush_pos,
                  a.prev_was_ff,
                  b.state,
                  b.code,
                  b.buf_len,
                  b.flush_pos,
                  b.prev_was_ff);
    return msg;
  }
  if (a.work_tail != b.work_tail) {
    return "work buffer mismatch";
  }
  return {};
}

// ------------------------------------------------------- incremental decode ---

struct inc_dec_result {
  byte_vec_t out;
  cobs_ret_t ret;
  unsigned state, code, block;
  bool complete;
  size_t consumed;
};

template <typename BeginFn, typename IncFn>
inc_dec_result run_dec_inc(BeginFn begin,
                           IncFn inc,
                           byte_t const* enc,
                           size_t enc_len,
                           size_t src_chunk,
                           size_t dst_chunk) {
  inc_dec_result r{};
  cobs_decode_inc_ctx_t ctx;
  r.ret = begin(&ctx);
  if (r.ret != COBS_RET_SUCCESS) {
    return r;
  }
  byte_vec_t chunk(dst_chunk);
  r.complete = false;
  while (!r.complete && (r.consumed < enc_len)) {
    size_t const take =
        ((enc_len - r.consumed) < src_chunk) ? (enc_len - r.consumed) : src_chunk;
    size_t src_used = 0, dst_used = 0;
    cobs_decode_inc_args_t const args{ enc + r.consumed, chunk.data(), take, dst_chunk };
    r.ret = inc(&ctx, &args, &src_used, &dst_used, &r.complete);
    if (r.ret != COBS_RET_SUCCESS) {
      return r;
    }
    r.out.insert(r.out.end(), chunk.data(), chunk.data() + dst_used);
    r.consumed += src_used;
    if (!src_used && !dst_used) {
      break;
    }
  }
  r.state = static_cast<unsigned>(ctx.state);
  r.code = ctx.code;
  r.block = ctx.block;
  return r;
}

std::string diff_decode_inc(byte_t const* enc,
                            size_t enc_len,
                            size_t src_chunk,
                            size_t dst_chunk) {
  inc_dec_result const a = run_dec_inc(cobs_ref_decode_inc_begin,
                                       cobs_ref_decode_inc,
                                       enc,
                                       enc_len,
                                       src_chunk,
                                       dst_chunk);
  inc_dec_result const b = run_dec_inc(cobs_decode_inc_begin,
                                       cobs_decode_inc,
                                       enc,
                                       enc_len,
                                       src_chunk,
                                       dst_chunk);
  char msg[160];
  if (a.ret != b.ret) {
    std::snprintf(msg, sizeof(msg), "ret mismatch: ref=%d swar=%d", a.ret, b.ret);
    return msg;
  }
  if (a.complete != b.complete) {
    return "complete mismatch";
  }
  if (a.consumed != b.consumed) {
    std::snprintf(msg,
                  sizeof(msg),
                  "consumed mismatch: ref=%zu swar=%zu",
                  a.consumed,
                  b.consumed);
    return msg;
  }
  if (a.out != b.out) {
    if (a.out.size() != b.out.size()) {
      return "dec len mismatch";
    }
    return first_diff(a.out.data(), b.out.data(), a.out.size());
  }
  if ((a.ret == COBS_RET_SUCCESS) &&
      ((a.state != b.state) || (a.code != b.code) || (a.block != b.block))) {
    return "ctx mismatch";
  }
  return {};
}

// ------------------------------------------------------------------ drivers ---

byte_vec_t encode_ok(byte_t const* p, size_t n) {
  byte_vec_t enc(COBS_ENCODE_MAX(n));
  size_t enc_len = 0;
  byte_t dummy = 0;
  REQUIRE(cobs_ref_encode(n ? p : &dummy, n, enc.data(), enc.size(), &enc_len) ==
          COBS_RET_SUCCESS);
  enc.resize(enc_len);
  return enc;
}

// Full round trip through every entry point for one payload.
std::string diff_all(byte_t const* p, size_t n) {
  size_t const emax = COBS_ENCODE_MAX(n);
  std::string e = diff_encode(p, n, emax, 0, 0);
  if (!e.empty()) {
    return "encode: " + e;
  }
  byte_vec_t const enc = encode_ok(p, n);
  e = diff_decode(enc.data(), enc.size(), n + 2, 0, 0);
  if (!e.empty()) {
    return "decode: " + e;
  }
  e = diff_encode_inc(p, n, 7, 5, 0);
  if (!e.empty()) {
    return "encode_inc: " + e;
  }
  e = diff_decode_inc(enc.data(), enc.size(), 7, 5);
  if (!e.empty()) {
    return "decode_inc: " + e;
  }
  return {};
}

void check(byte_t const* p, size_t n, char const* what) {
  std::string const e = diff_all(p, n);
  if (!e.empty()) {
    FAIL("DIFFERENTIAL FAILURE (" << what << ") len=" << n << "\n  payload: " << hex(p, n)
                                  << "\n  " << e);
  }
}

}  // namespace

// Only zero-vs-nonzero matters to COBS, so {0x00, 0x01} is a complete behavioral
// model. Length 0..16 covers every zero pattern within two 64-bit words.
TEST_CASE("SWAR differential: exhaustive binary alphabet, len 0..16") {
  byte_t p[20];
  for (size_t len = 0; len <= 16; ++len) {
    uint32_t const combos = (len >= 32) ? 0u : (1u << len);
    for (uint32_t m = 0; m < combos; ++m) {
      for (size_t i = 0; i < len; ++i) {
        p[i] = ((m >> i) & 1u) ? byte_t{ 0x01 } : byte_t{ 0x00 };
      }
      std::string const e = diff_all(p, len);
      if (!e.empty()) {
        FAIL("DIFFERENTIAL FAILURE len=" << len << " mask=0x" << std::hex << m << std::dec
                                         << "\n  " << e);
      }
    }
  }
}

TEST_CASE("SWAR differential: exhaustive ternary alphabet, len 0..8" *
          doctest::test_suite("slow")) {
  byte_t const alpha[3] = { 0x00, 0x01, 0xFF };
  byte_t p[12];
  for (size_t len = 0; len <= 8; ++len) {
    size_t combos = 1;
    for (size_t i = 0; i < len; ++i) {
      combos *= 3;
    }
    for (size_t m = 0; m < combos; ++m) {
      size_t t = m;
      for (size_t i = 0; i < len; ++i) {
        p[i] = alpha[t % 3];
        t /= 3;
      }
      std::string const e = diff_all(p, len);
      if (!e.empty()) {
        FAIL("DIFFERENTIAL FAILURE len=" << len << " idx=" << m << "\n  " << e);
      }
    }
  }
}

// The pairs are the point: they catch the classic word-at-a-time bug of finding
// the first zero in a word and missing the second one in the same word.
TEST_CASE("SWAR differential: single zero at every position") {
  byte_vec_t p;
  for (size_t len = 1; len <= 80; ++len) {
    p.assign(len, 0xAA);
    for (size_t i = 0; i < len; ++i) {
      p[i] = 0x00;
      check(p.data(), len, "single zero");
      p[i] = 0xAA;
    }
  }
}

TEST_CASE("SWAR differential: two zeros at every pair of positions") {
  byte_vec_t p;
  for (size_t len = 2; len <= 40; ++len) {
    p.assign(len, 0xAA);
    for (size_t i = 0; i < len; ++i) {
      for (size_t j = i + 1; j < len; ++j) {
        p[i] = 0x00;
        p[j] = 0x00;
        check(p.data(), len, "two zeros");
        p[i] = 0xAA;
        p[j] = 0xAA;
      }
    }
  }
}

// The forced 0xFF block break is the one thing a fast lane must key on a counter
// for rather than on data, so exercise it at every word phase.
TEST_CASE("SWAR differential: long runs at every word phase") {
  size_t const runs[] = { 252, 253, 254, 255, 256, 507, 508, 509, 510, 762, 763, 764 };
  for (size_t const run : runs) {
    for (size_t lead = 0; lead <= 16; ++lead) {
      for (size_t trail = 0; trail <= 16; trail += 4) {
        byte_vec_t p;
        p.reserve(lead + 1 + run + 1 + trail);
        p.insert(p.end(), lead, 0x11);
        if (lead) {
          p.push_back(0x00);
        }
        p.insert(p.end(), run, 0x22);
        p.push_back(0x00);
        p.insert(p.end(), trail, 0x33);
        check(p.data(), p.size(), "long run");
      }
    }
  }
}

// EXHAUSTED must still fire at the identical byte, and nothing may be written at
// or past the limit -- which the guard page enforces absolutely.
TEST_CASE("SWAR differential: destination exhaustion sweep") {
  size_t const lens[] = { 0, 1, 7, 8, 9, 15, 16, 17, 31, 63, 64, 65, 253, 254, 255, 256 };
  for (size_t const len : lens) {
    for (int which = 0; which < 3; ++which) {
      byte_vec_t p(len, 0x77);
      if (which == 1) {
        p.assign(len, 0x00);
      } else if ((which == 2) && len) {
        for (size_t i = 0; i < len; ++i) {
          p[i] = ((i % 9) == 8) ? byte_t{ 0x00 } : byte_t{ 0x55 };
        }
      }
      size_t const exact = COBS_ENCODE_MAX(len);
      for (size_t emax = 0; emax <= exact + 2; ++emax) {
        std::string const e = diff_encode(p.data(), len, emax, 0, 0);
        if (!e.empty()) {
          FAIL("encode enc_max=" << emax << " len=" << len << " which=" << which << "\n  "
                                 << e);
        }
      }
      byte_vec_t const enc = encode_ok(p.data(), len);
      for (size_t dmax = 0; dmax <= len + 2; ++dmax) {
        std::string const e = diff_decode(enc.data(), enc.size(), dmax, 0, 0);
        if (!e.empty()) {
          FAIL("decode dec_max=" << dmax << " len=" << len << " which=" << which << "\n  "
                                 << e);
        }
      }
    }
  }
}

// Where a word-at-a-time validator is most likely to diverge: the byte loop bails
// on the first embedded zero, while a fast lane may have written more first.
TEST_CASE("SWAR differential: single-byte mutations of valid frames") {
  size_t const lens[] = { 1, 5, 8, 9, 16, 17, 31, 64, 255, 260 };
  byte_t const muts[] = { 0x00, 0x01, 0x02, 0x7F, 0xFE, 0xFF };
  for (size_t const len : lens) {
    byte_vec_t p(len);
    for (size_t i = 0; i < len; ++i) {
      p[i] = static_cast<byte_t>(1u + (i % 254u));
    }
    byte_vec_t const good = encode_ok(p.data(), len);
    for (size_t i = 0; i < good.size(); ++i) {
      for (byte_t const mv : muts) {
        if (good[i] == mv) {
          continue;
        }
        byte_vec_t bad = good;
        bad[i] = mv;
        std::string e = diff_decode(bad.data(), bad.size(), len + 2, 0, 0);
        if (!e.empty()) {
          FAIL("decode mutation len=" << len << " at " << i << " -> " << int(mv) << "\n  "
                                      << e);
        }
        e = diff_decode_inc(bad.data(), bad.size(), 3, 3);
        if (!e.empty()) {
          FAIL("decode_inc mutation len=" << len << " at " << i << " -> " << int(mv)
                                          << "\n  " << e);
        }
        e = diff_tinyframe(bad.data(), bad.size(), 0, false);
        if (!e.empty()) {
          FAIL("decode_tinyframe mutation len=" << len << " at " << i << "\n  " << e);
        }
      }
    }
  }
}

// Randomized, fixed-seed, matching the existing deterministic-stress convention.
TEST_CASE("SWAR differential: randomized") {
  std::mt19937 rng{ 20260915u };
  size_t const iters = 3000;
  for (size_t it = 0; it < iters; ++it) {
    size_t const len = rng() % 1024u;
    unsigned const zero_in = 1u + (rng() % 64u);  // 1-in-N bytes is zero
    byte_vec_t p(len);
    for (size_t i = 0; i < len; ++i) {
      p[i] = ((rng() % zero_in) == 0) ? byte_t{ 0 }
                                      : static_cast<byte_t>(1u + (rng() % 255u));
    }
    std::string e = diff_all(p.data(), len);
    if (!e.empty()) {
      FAIL("randomized it=" << it << " len=" << len << " zero_in=" << zero_in << "\n  "
                            << e);
    }
    size_t const sc = 1u + (rng() % 300u), dc = 1u + (rng() % 300u);
    e = diff_encode_inc(p.data(), len, sc, dc, rng() % 8u);
    if (!e.empty()) {
      FAIL("randomized encode_inc it=" << it << " len=" << len << " src_chunk=" << sc
                                       << " dst_chunk=" << dc << "\n  " << e);
    }
    byte_vec_t const enc = encode_ok(p.data(), len);
    e = diff_decode_inc(enc.data(), enc.size(), sc, dc);
    if (!e.empty()) {
      FAIL("randomized decode_inc it=" << it << " len=" << len << "\n  " << e);
    }
  }
}
