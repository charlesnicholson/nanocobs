#pragma once

#include "byte_vec.h"

#include <cstdint>
#include <random>
#include <string>

namespace cobs_test {

// Shared by the differential tests and the benchmark, so what gets measured is what
// gets validated. Every shape moves one knob: the run lengths between zero bytes.
enum class shape {
  nonzero,         // no zeros at all: 254-byte blocks, fast lane always engaged
  all_zero,        // regression canary: fast lane never usable
  bernoulli_p254,  // one zero per nominal block
  bernoulli_1,     // 1% zeros
  bernoulli_5,     // 5%
  bernoulli_25,    // 25%
  bernoulli_50,    // 50%
  zero_every_8,    // zeros exactly on 64-bit word boundaries
  zero_every_9,    // deliberately phase-shifted off every word boundary
  zero_every_16,
  zero_every_17,
  zero_every_254,  // every block exactly 254: zero and block-end coincide
  zero_every_255,  // maximal back-to-back 0xFF blocks with no zero to key on
  clustered,       // long nonzero runs punctuated by short zero bursts
  zero_run_32,     // alternating 32-byte data and 32-byte zero runs
  zero_run_8,      // alternating 8-byte data and 8-byte zero runs
  sparse,          // long zero runs broken by occasional short data bursts
  text,            // printable ASCII, no zeros
};

inline char const* shape_name(shape s) {
  switch (s) {
    case shape::nonzero:
      return "nonzero";
    case shape::all_zero:
      return "all_zero";
    case shape::bernoulli_p254:
      return "z_1of254";
    case shape::bernoulli_1:
      return "z_1pct";
    case shape::bernoulli_5:
      return "z_5pct";
    case shape::bernoulli_25:
      return "z_25pct";
    case shape::bernoulli_50:
      return "z_50pct";
    case shape::zero_every_8:
      return "z_every_8";
    case shape::zero_every_9:
      return "z_every_9";
    case shape::zero_every_16:
      return "z_every_16";
    case shape::zero_every_17:
      return "z_every_17";
    case shape::zero_every_254:
      return "z_every_254";
    case shape::zero_every_255:
      return "z_every_255";
    case shape::clustered:
      return "clustered";
    case shape::zero_run_32:
      return "zero_run_32";
    case shape::zero_run_8:
      return "zero_run_8";
    case shape::sparse:
      return "sparse";
    case shape::text:
      return "text";
  }
  return "?";
}

inline shape const* all_shapes(size_t* out_n) {
  static shape const s[] = {
    shape::nonzero,        shape::all_zero,      shape::bernoulli_p254,
    shape::bernoulli_1,    shape::bernoulli_5,   shape::bernoulli_25,
    shape::bernoulli_50,   shape::zero_every_8,  shape::zero_every_9,
    shape::zero_every_16,  shape::zero_every_17, shape::zero_every_254,
    shape::zero_every_255, shape::clustered,     shape::text,
    shape::zero_run_32,    shape::zero_run_8,    shape::sparse,
  };
  *out_n = sizeof(s) / sizeof(s[0]);
  return s;
}

inline void gen_shape(shape s, byte_t* dst, size_t n, uint32_t seed) {
  std::mt19937 rng{ seed };
  auto nz = [&rng]() -> byte_t {
    return static_cast<byte_t>(1u + (rng() % 255u));  // uniform in [1, 255]
  };
  auto every = [&](size_t k) {
    for (size_t i = 0; i < n; ++i) {
      dst[i] = ((i % k) == (k - 1)) ? byte_t{ 0 } : nz();
    }
  };
  auto bern = [&](double p) {
    std::bernoulli_distribution d{ p };
    for (size_t i = 0; i < n; ++i) {
      dst[i] = d(rng) ? byte_t{ 0 } : nz();
    }
  };

  switch (s) {
    case shape::nonzero:
      for (size_t i = 0; i < n; ++i) {
        dst[i] = nz();
      }
      break;
    case shape::all_zero:
      for (size_t i = 0; i < n; ++i) {
        dst[i] = 0;
      }
      break;
    case shape::bernoulli_p254:
      bern(1.0 / 254.0);
      break;
    case shape::bernoulli_1:
      bern(0.01);
      break;
    case shape::bernoulli_5:
      bern(0.05);
      break;
    case shape::bernoulli_25:
      bern(0.25);
      break;
    case shape::bernoulli_50:
      bern(0.50);
      break;
    case shape::zero_every_8:
      every(8);
      break;
    case shape::zero_every_9:
      every(9);
      break;
    case shape::zero_every_16:
      every(16);
      break;
    case shape::zero_every_17:
      every(17);
      break;
    case shape::zero_every_254:
      every(254);
      break;
    case shape::zero_every_255:
      every(255);
      break;
    case shape::clustered: {
      size_t i = 0;
      while (i < n) {
        size_t run = 1024u + (rng() % 3072u);
        for (size_t k = 0; (k < run) && (i < n); ++k, ++i) {
          dst[i] = nz();
        }
        size_t burst = 1u + (rng() % 8u);
        for (size_t k = 0; (k < burst) && (i < n); ++k, ++i) {
          dst[i] = 0;
        }
      }
    } break;
    case shape::zero_run_32:
      for (size_t i = 0; i < n; ++i) {
        dst[i] = ((i / 32u) & 1u) ? byte_t{ 0 } : nz();
      }
      break;
    case shape::zero_run_8:
      for (size_t i = 0; i < n; ++i) {
        dst[i] = ((i / 8u) & 1u) ? byte_t{ 0 } : nz();
      }
      break;
    case shape::sparse: {
      size_t i = 0;
      while (i < n) {
        size_t const run = 48u + (rng() % 208u);
        for (size_t k = 0; (k < run) && (i < n); ++k, ++i) {
          dst[i] = 0;
        }
        size_t const burst = 1u + (rng() % 12u);
        for (size_t k = 0; (k < burst) && (i < n); ++k, ++i) {
          dst[i] = nz();
        }
      }
    } break;
    case shape::text:
      for (size_t i = 0; i < n; ++i) {
        dst[i] = static_cast<byte_t>(0x20u + (rng() % 95u));
      }
      break;
  }
}

inline byte_vec_t make_shape(shape s, size_t n, uint32_t seed) {
  byte_vec_t v(n);
  if (n) {
    gen_shape(s, v.data(), n, seed);
  }
  return v;
}

// Mean number of payload bytes between zeros -- the single best predictor of both
// encode and decode cost, and the axis on which SWAR wins or loses.
inline double mean_run_len(byte_t const* p, size_t n) {
  size_t zeros = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!p[i]) {
      ++zeros;
    }
  }
  return zeros ? (double(n) / double(zeros)) : double(n);
}

}  // namespace cobs_test
