#include "../cobs.h"
#include "byte_vec.h"
#include "cobs_ref.h"
#include "doctest_wrapper.h"

#include <cstring>
#include <string>

// Runs are what the SWAR lanes key on: one lane copies a run of data, the other
// emits a run of empty blocks. Every run length from 1 to past a 0xFF block.

namespace {

byte_t constexpr kPoison = 0xCD;
byte_t constexpr kData = 0x41;

struct enc_result {
  byte_vec_t buf;
  size_t len;
  cobs_ret_t ret;
  size_t consumed{ 0u };  // decode only; comparing it diffs consumed on every payload
  bool operator==(enc_result const& o) const {
    return (ret == o.ret) && (len == o.len) && (consumed == o.consumed) &&
           (buf == o.buf);
  }
};

template <typename Fn>
enc_result encode_with(Fn fn, byte_vec_t const& dec) {
  enc_result r{ byte_vec_t(COBS_ENCODE_MAX(dec.size()) + 8u, kPoison),
                0,
                COBS_RET_SUCCESS };
  byte_t dummy = 0;
  r.ret = fn(dec.empty() ? &dummy : dec.data(),
             dec.size(),
             r.buf.data(),
             r.buf.size(),
             &r.len);
  return r;
}

template <typename Fn>
enc_result decode_with(Fn fn, byte_t const* enc, size_t enc_len, size_t dec_max) {
  enc_result r{ byte_vec_t(dec_max + 8u, kPoison), 0, COBS_RET_SUCCESS };
  r.ret = fn(enc, enc_len, r.buf.data(), r.buf.size(), &r.len, &r.consumed);
  return r;
}

// Every entry point, SWAR against the scalar byte loop, for one payload.
void check(byte_vec_t const& dec, std::string const& what) {
  enc_result const a = encode_with(cobs_ref_encode, dec);
  enc_result const b = encode_with(cobs_encode, dec);
  if (!(a == b)) {
    FAIL("encode differs: " << what << " len=" << dec.size() << " ref_ret=" << a.ret
                            << " swar_ret=" << b.ret << " ref_len=" << a.len
                            << " swar_len=" << b.len);
  }
  REQUIRE(a.ret == COBS_RET_SUCCESS);

  enc_result const da = decode_with(cobs_ref_decode, b.buf.data(), b.len, dec.size() + 2u);
  enc_result const db = decode_with(cobs_decode, b.buf.data(), b.len, dec.size() + 2u);
  if (!(da == db)) {
    FAIL("decode differs: " << what << " len=" << dec.size());
  }
  REQUIRE(da.ret == COBS_RET_SUCCESS);
  REQUIRE(da.len == dec.size());
  if (!dec.empty()) {
    REQUIRE(std::memcmp(da.buf.data(), dec.data(), dec.size()) == 0);
  }

  // In-place decode, the aliasing configuration the fast lanes have to respect.
  byte_vec_t inplace(b.buf.data(), b.buf.data() + b.len);
  size_t ip_len = 0, ip_consumed = 0;
  REQUIRE(cobs_decode(inplace.data(), b.len, inplace.data(), b.len, &ip_len,
                      &ip_consumed) == COBS_RET_SUCCESS);
  REQUIRE(ip_len == dec.size());
  REQUIRE(ip_consumed == b.len);  // aliasing must not disturb the count
  if (!dec.empty()) {
    REQUIRE(std::memcmp(inplace.data(), dec.data(), dec.size()) == 0);
  }
}

byte_vec_t run_of(size_t n, byte_t v) {
  return byte_vec_t(n, v);
}

void cat(byte_vec_t& dst, size_t n, byte_t v) {
  dst.insert(dst.end(), n, v);
}

// Lengths that bracket a machine word, a 0xFF code block, and two of each.
size_t constexpr kBounds[] = {
  1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,
  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  62,  63,  64,  65,  66,
  126, 127, 128, 129, 130, 252, 253, 254, 255, 256, 257, 258, 508, 509, 510
};

}  // namespace

TEST_CASE("Runs: zeroes, 1 to 256 bytes") {
  for (size_t n = 1; n <= 256; ++n) {
    check(run_of(n, 0x00), "zero run");
  }
}

TEST_CASE("Runs: nonzeroes, 1 to 256 bytes") {
  for (size_t n = 1; n <= 256; ++n) {
    check(run_of(n, kData), "data run");
    if (n <= 64) {  // a run whose bytes all differ, so a stuck lane shows up
      byte_vec_t v(n);
      for (size_t i = 0; i < n; ++i) {
        v[i] = static_cast<byte_t>(1u + (i % 255u));
      }
      check(v, "ascending data run");
    }
  }
}

TEST_CASE("Runs: zeroes then data, both across every boundary") {
  for (size_t z : kBounds) {
    for (size_t d : kBounds) {
      if ((z + d) > 600u) {
        continue;
      }
      byte_vec_t v;
      cat(v, z, 0x00);
      cat(v, d, kData);
      check(v, "zeroes then data");
    }
  }
}

TEST_CASE("Runs: data then zeroes, both across every boundary") {
  for (size_t d : kBounds) {
    for (size_t z : kBounds) {
      if ((z + d) > 600u) {
        continue;
      }
      byte_vec_t v;
      cat(v, d, kData);
      cat(v, z, 0x00);
      check(v, "data then zeroes");
    }
  }
}

TEST_CASE("Runs: data sandwiched in zeroes, and zeroes sandwiched in data") {
  for (size_t outer : kBounds) {
    for (size_t inner : kBounds) {
      if (((2u * outer) + inner) > 600u) {
        continue;
      }
      byte_vec_t a;
      cat(a, outer, 0x00);
      cat(a, inner, kData);
      cat(a, outer, 0x00);
      check(a, "data in zeroes");

      byte_vec_t b;
      cat(b, outer, kData);
      cat(b, inner, 0x00);
      cat(b, outer, kData);
      check(b, "zeroes in data");
    }
  }
}

TEST_CASE("Runs: alternating, every period 1 to 40") {
  for (size_t period = 1; period <= 40; ++period) {
    for (size_t reps = 1; reps <= 6; ++reps) {
      byte_vec_t v;
      for (size_t r = 0; r < reps; ++r) {
        cat(v, period, kData);
        cat(v, period, 0x00);
      }
      check(v, "alternating");
      v.push_back(kData);  // break the period at the tail
      check(v, "alternating, odd tail");
    }
  }
}

TEST_CASE("Runs: a 0xFF block boundary at every offset into a zero run") {
  // 254 data bytes force a 0xFF code block; sliding zeroes in front of it moves
  // that forced break to every possible word phase.
  for (size_t lead = 0; lead <= 40; ++lead) {
    for (size_t run : { 252u, 253u, 254u, 255u, 256u, 507u, 508u, 509u }) {
      byte_vec_t v;
      cat(v, lead, 0x00);
      cat(v, run, kData);
      cat(v, lead, 0x00);
      check(v, "zero-led 0xFF block");
    }
  }
}

TEST_CASE("Runs: tinyframe agrees with the scalar build at every safe length") {
  for (size_t payload = 0; payload <= 254; ++payload) {
    for (int shape = 0; shape < 3; ++shape) {
      byte_vec_t p(payload);
      for (size_t i = 0; i < payload; ++i) {
        p[i] = (shape == 0)
                   ? byte_t{ 0x00 }
                   : ((shape == 1) ? kData : static_cast<byte_t>((i % 5u) ? kData : 0x00));
      }
      byte_vec_t frame;
      frame.push_back(COBS_TINYFRAME_SENTINEL_VALUE);
      frame.insert(frame.end(), p.begin(), p.end());
      frame.push_back(COBS_TINYFRAME_SENTINEL_VALUE);

      byte_vec_t a = frame, b = frame;
      REQUIRE(cobs_ref_encode_tinyframe(a.data(), a.size()) ==
              cobs_encode_tinyframe(b.data(), b.size()));
      if (a != b) {
        FAIL("tinyframe encode differs: payload=" << payload << " shape=" << shape);
      }
      REQUIRE(cobs_ref_decode_tinyframe(a.data(), a.size()) ==
              cobs_decode_tinyframe(b.data(), b.size()));
      if (a != b) {
        FAIL("tinyframe decode differs: payload=" << payload << " shape=" << shape);
      }
      REQUIRE(b == frame);
    }
  }
}

TEST_CASE("Runs: payloads of 0x01, the byte the decode lane pattern-matches on") {
  // A run of 0x01 code bytes is what the decoder's zero-run lane looks for, and
  // 0x01 is also a perfectly ordinary payload byte. The two must not be confused.
  for (size_t n = 1; n <= 600; ++n) {
    check(run_of(n, 0x01), "0x01 run");
  }
  for (size_t lead = 0; lead <= 24; ++lead) {
    for (size_t n : { 1u, 7u, 8u, 9u, 16u, 17u, 253u, 254u, 255u, 256u }) {
      byte_vec_t v;
      cat(v, lead, 0x00);
      cat(v, n, 0x01);
      cat(v, lead, 0x00);
      check(v, "0x01 run between zero runs");
    }
  }
}

TEST_CASE("Runs: alternating 0x00 and 0x01, every length to 600") {
  // Neither lane can ever fire: every word holds both a zero and a nonzero.
  for (size_t n = 1; n <= 600; ++n) {
    byte_vec_t v(n);
    for (size_t i = 0; i < n; ++i) {
      v[i] = (i & 1u) ? byte_t{ 0x01 } : byte_t{ 0x00 };
    }
    check(v, "alternating 00/01");
    for (size_t i = 0; i < n; ++i) {
      v[i] = (i & 1u) ? byte_t{ 0x00 } : byte_t{ 0x01 };
    }
    check(v, "alternating 01/00");
  }
}

TEST_CASE("Runs: a forced 0xFF block ending exactly on a zero") {
  // 254 data bytes fill a block; the byte right after decides whether the encoder
  // emits a fresh code byte or lets the delimiter stand in for it.
  for (size_t data : { 252u, 253u, 254u, 255u, 256u, 507u, 508u, 509u, 510u }) {
    for (size_t zeros = 0; zeros <= 8; ++zeros) {
      byte_vec_t v;
      cat(v, data, kData);
      cat(v, zeros, 0x00);
      check(v, "0xFF block then zeroes");
      cat(v, 1, kData);
      check(v, "0xFF block, zeroes, one byte");
    }
  }
}
