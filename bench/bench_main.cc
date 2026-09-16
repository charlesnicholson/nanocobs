// Both implementations live in this binary, timed A/B interleaved with alternating
// order so drift hits both equally. Not a doctest case: that binary is -Os + asan.

#include "../cobs.h"
#include "../tests/cobs_ref.h"
#include "../tests/cobs_shapes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef COBS_BENCH_REV
#define COBS_BENCH_REV "unknown"
#endif

namespace {

using clock_t_ = std::chrono::steady_clock;
using cobs_test::shape;

// ------------------------------------------------------------- measurement --

template <typename T>
void sink(T const& v) {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("" : : "r,m"(v) : "memory");
#else
  static volatile char x;
  x = *reinterpret_cast<volatile char const*>(&v);
#endif
}

void clobber() {
#if defined(__GNUC__) || defined(__clang__)
  __asm__ __volatile__("" : : : "memory");
#endif
}

void spin_warmup(int ms) {
  auto const end = clock_t_::now() + std::chrono::milliseconds(ms);
  uint64_t x = 1;
  while (clock_t_::now() < end) {
    x = x * 6364136223846793005ull + 1442695040888963407ull;
  }
  sink(x);
}

template <typename F>
uint64_t time_batch(F&& f, size_t reps) {
  clobber();
  auto const t0 = clock_t_::now();
  for (size_t i = 0; i < reps; ++i) {
    f();
  }
  auto const t1 = clock_t_::now();
  clobber();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
}

template <typename F>
size_t calibrate(F&& f, uint64_t target_ns) {
  size_t reps = 1;
  for (int i = 0; i < 64; ++i) {
    uint64_t const ns = time_batch(f, reps);
    if (ns >= target_ns) {
      return reps;
    }
    double const grow =
        (ns == 0)
            ? 8.0
            : std::min(8.0,
                       1.25 * static_cast<double>(target_ns) / static_cast<double>(ns));
    size_t const next = static_cast<size_t>(static_cast<double>(reps) * grow);
    reps = std::max(reps + 1, next);
  }
  return reps;
}

uint64_t clock_resolution_ns() {
  uint64_t best = UINT64_MAX;
  for (int i = 0; i < 1000; ++i) {
    auto const a = clock_t_::now();
    clock_t_::time_point b;
    do {
      b = clock_t_::now();
    } while (b == a);
    uint64_t const d = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
    best = std::min(best, d);
  }
  return best;
}

uint64_t clock_overhead_ns() {
  size_t const n = 100000;
  auto const t0 = clock_t_::now();
  for (size_t i = 0; i < n; ++i) {
    sink(clock_t_::now());
  }
  auto const t1 = clock_t_::now();
  return static_cast<uint64_t>(
             std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
         n;
}

// --------------------------------------------------------------- reporting --

struct config {
  uint64_t batch_ns = 50'000'000;
  int samples = 15;
  std::string filter;
  double cpu_ghz = 0.0;
};

struct row {
  char const* api;
  char const* shape_name;
  size_t bytes;
  size_t src_ofs, dst_ofs;
  size_t chunk;
  double blklen;
  uint64_t min_ref, min_swar;
  double paired;  // median of per-round t_ref/t_swar
  uint64_t ck_ref, ck_swar;
  size_t reps;
  uint64_t med_swar;
};

std::vector<row> g_rows;

uint64_t fnv(uint64_t h, byte_t const* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    h = (h ^ p[i]) * 1099511628211ull;
  }
  return h;
}

// Times |ref| and |swar| interleaved, alternating order each round.
template <typename RefF, typename SwarF>
void measure(config const& cfg,
             char const* api,
             shape sh,
             size_t bytes,
             size_t src_ofs,
             size_t dst_ofs,
             size_t chunk,
             double blklen,
             uint64_t ck_ref,
             uint64_t ck_swar,
             RefF&& ref,
             SwarF&& swar) {
  std::string const label = std::string(api) + "/" + cobs_test::shape_name(sh);
  if (!cfg.filter.empty() && (label.find(cfg.filter) == std::string::npos)) {
    return;
  }

  size_t const reps = calibrate(swar, cfg.batch_ns);
  time_batch(ref, reps);  // per-case warmup, discarded
  time_batch(swar, reps);

  std::vector<double> ratios;
  uint64_t min_ref = UINT64_MAX, min_swar = UINT64_MAX;
  std::vector<uint64_t> swar_samples;
  for (int r = 0; r < cfg.samples; ++r) {
    bool const ref_first = (r % 2) == 0;
    uint64_t a = 0, b = 0;
    if (ref_first) {
      a = time_batch(ref, reps);
      b = time_batch(swar, reps);
    } else {
      b = time_batch(swar, reps);
      a = time_batch(ref, reps);
    }
    min_ref = std::min(min_ref, a);
    min_swar = std::min(min_swar, b);
    swar_samples.push_back(b);
    if (b) {
      ratios.push_back(static_cast<double>(a) / static_cast<double>(b));
    }
  }
  std::sort(ratios.begin(), ratios.end());
  std::sort(swar_samples.begin(), swar_samples.end());

  row const rw{ api,
                cobs_test::shape_name(sh),
                bytes,
                src_ofs,
                dst_ofs,
                chunk,
                blklen,
                min_ref / reps,
                min_swar / reps,
                ratios.empty() ? 0.0 : ratios[ratios.size() / 2],
                ck_ref,
                ck_swar,
                reps,
                swar_samples[swar_samples.size() / 2] / reps };
  g_rows.push_back(rw);
}

void print_rows(config const& cfg) {
  std::printf(
      "impl\tapi\tshape\tbytes\tsrc_ofs\tdst_ofs\tchunk\tblklen\treps\tns\tMiBps\tns_"
      "byte%s\tck\n",
      (cfg.cpu_ghz > 0.0) ? "\tcyc_byte" : "");
  for (row const& r : g_rows) {
    for (int impl = 0; impl < 2; ++impl) {
      uint64_t const ns = impl ? r.min_swar : r.min_ref;
      double const nsb =
          static_cast<double>(ns) / static_cast<double>(r.bytes ? r.bytes : 1);
      double const mibps = (ns == 0) ? 0.0
                                     : (static_cast<double>(r.bytes) * 1e9) /
                                           (static_cast<double>(ns) * 1048576.0);
      std::printf("%s\t%s\t%s\t%zu\t%zu\t%zu\t%zu\t%.1f\t%zu\t%llu\t%.1f\t%.4f",
                  impl ? "swar" : "ref",
                  r.api,
                  r.shape_name,
                  r.bytes,
                  r.src_ofs,
                  r.dst_ofs,
                  r.chunk,
                  r.blklen,
                  r.reps,
                  static_cast<unsigned long long>(ns),
                  mibps,
                  nsb);
      if (cfg.cpu_ghz > 0.0) {
        std::printf("\t%.3f", nsb * cfg.cpu_ghz);
      }
      std::printf("\t0x%llx\n",
                  static_cast<unsigned long long>(impl ? r.ck_swar : r.ck_ref));
    }
  }
}

void print_summary() {
  std::printf("\n# ---- paired speedup (ref/swar; >1 means SWAR is faster) ----\n");
  std::printf("# api\tshape\tbytes\tchunk\tmin_ratio\tpaired\tverdict\n");
  double worst = 1e9;
  char const* worst_api = "";
  char const* worst_shape = "";
  size_t worst_bytes = 0;
  double log_sum = 0.0;
  size_t log_n = 0;
  bool checksum_bad = false;
  for (row const& r : g_rows) {
    double const mr =
        r.min_swar ? (static_cast<double>(r.min_ref) / static_cast<double>(r.min_swar))
                   : 0.0;
    char const* verdict = (mr >= 1.10) ? "win" : ((mr <= 0.95) ? "REGRESSION" : "neutral");
    if (r.ck_ref != r.ck_swar) {
      verdict = "CHECKSUM-MISMATCH";
      checksum_bad = true;
    }
    std::printf("  %s\t%s\t%zu\t%zu\t%.2fx\t%.2fx\t%s\n",
                r.api,
                r.shape_name,
                r.bytes,
                r.chunk,
                mr,
                r.paired,
                verdict);
    if (mr > 0.0) {
      log_sum += std::log(mr);
      ++log_n;
      if (mr < worst) {
        worst = mr;
        worst_api = r.api;
        worst_shape = r.shape_name;
        worst_bytes = r.bytes;
      }
    }
  }
  if (log_n) {
    std::printf("# geomean %.2fx over %zu rows; worst %.2fx (%s/%s/%zu)\n",
                std::exp(log_sum / static_cast<double>(log_n)),
                log_n,
                worst,
                worst_api,
                worst_shape,
                worst_bytes);
  }
  std::printf(
      "# Decision rule: do not claim a win below 1.10x; do not accept a regression\n"
      "# worse than 0.95x on any row. Function alignment and link order alone move\n"
      "# microbenchmarks by +-5%%.\n");
  if (checksum_bad) {
    std::printf(
        "# *** CHECKSUM MISMATCH: the two builds disagree. Results are void. ***\n");
  }
}

// ------------------------------------------------------------------- cases --

struct arena {
  std::vector<byte_t> mem;
  byte_t* at(size_t ofs) {
    return mem.data() + 64 + ofs;
  }
  explicit arena(size_t n) : mem(n + 128) {
  }
};

void bench_encode(config const& cfg, shape sh, size_t n, size_t src_ofs, size_t dst_ofs) {
  arena src(n), a(COBS_ENCODE_MAX(n)), b(COBS_ENCODE_MAX(n));
  cobs_test::gen_shape(sh, src.at(src_ofs), n, 0xB0B0u);
  size_t const emax = COBS_ENCODE_MAX(n);

  size_t la = 0, lb = 0;
  cobs_ref_encode(src.at(src_ofs), n, a.at(dst_ofs), emax, &la);
  cobs_encode(src.at(src_ofs), n, b.at(dst_ofs), emax, &lb);

  auto ref = [&] {
    size_t out = 0;
    sink(cobs_ref_encode(src.at(src_ofs), n, a.at(dst_ofs), emax, &out));
    sink(out);
  };
  auto swar = [&] {
    size_t out = 0;
    sink(cobs_encode(src.at(src_ofs), n, b.at(dst_ofs), emax, &out));
    sink(out);
  };
  measure(cfg,
          "encode",
          sh,
          n,
          src_ofs,
          dst_ofs,
          0,
          cobs_test::mean_run_len(src.at(src_ofs), n),
          fnv(la, a.at(dst_ofs), la),
          fnv(lb, b.at(dst_ofs), lb),
          ref,
          swar);
}

void bench_decode(config const& cfg, shape sh, size_t n, size_t src_ofs, size_t dst_ofs) {
  arena raw(n);
  cobs_test::gen_shape(sh, raw.at(0), n, 0xB0B0u);
  std::vector<byte_t> enc(COBS_ENCODE_MAX(n));
  size_t enc_len = 0;
  byte_t dummy = 0;
  cobs_ref_encode(n ? raw.at(0) : &dummy, n, enc.data(), enc.size(), &enc_len);

  arena src(enc_len), a(n + 2), b(n + 2);
  std::memcpy(src.at(src_ofs), enc.data(), enc_len);

  size_t la = 0, lb = 0;
  cobs_ref_decode(src.at(src_ofs), enc_len, a.at(dst_ofs), n + 2, &la);
  cobs_decode(src.at(src_ofs), enc_len, b.at(dst_ofs), n + 2, &lb);

  auto ref = [&] {
    size_t out = 0;
    sink(cobs_ref_decode(src.at(src_ofs), enc_len, a.at(dst_ofs), n + 2, &out));
    sink(out);
  };
  auto swar = [&] {
    size_t out = 0;
    sink(cobs_decode(src.at(src_ofs), enc_len, b.at(dst_ofs), n + 2, &out));
    sink(out);
  };
  measure(cfg,
          "decode",
          sh,
          n,
          src_ofs,
          dst_ofs,
          0,
          cobs_test::mean_run_len(raw.at(0), n),
          fnv(la, a.at(dst_ofs), la),
          fnv(lb, b.at(dst_ofs), lb),
          ref,
          swar);
}

void bench_tinyframe(config const& cfg,
                     shape sh,
                     size_t payload,
                     size_t ofs,
                     bool encode) {
  size_t const len = payload + 2;
  arena tmpl(len), a(len), b(len);
  cobs_test::gen_shape(sh, tmpl.at(0) + 1, payload, 0xB0B0u);
  tmpl.at(0)[0] = COBS_TINYFRAME_SENTINEL_VALUE;
  tmpl.at(0)[len - 1] = COBS_TINYFRAME_SENTINEL_VALUE;
  if (!encode) {
    cobs_ref_encode_tinyframe(tmpl.at(0), len);
  }

  auto ref = [&] {
    std::memcpy(a.at(ofs), tmpl.at(0), len);
    sink(encode ? cobs_ref_encode_tinyframe(a.at(ofs), len)
                : cobs_ref_decode_tinyframe(a.at(ofs), len));
  };
  auto swar = [&] {
    std::memcpy(b.at(ofs), tmpl.at(0), len);
    sink(encode ? cobs_encode_tinyframe(b.at(ofs), len)
                : cobs_decode_tinyframe(b.at(ofs), len));
  };
  ref();
  swar();
  measure(cfg,
          encode ? "enc_tinyframe" : "dec_tinyframe",
          sh,
          payload,
          ofs,
          ofs,
          0,
          cobs_test::mean_run_len(tmpl.at(0) + 1, payload),
          fnv(0, a.at(ofs), len),
          fnv(0, b.at(ofs), len),
          ref,
          swar);
}

template <typename BeginF, typename IncF, typename EndF>
uint64_t drive_enc_inc(BeginF begin,
                       IncF inc,
                       EndF end,
                       byte_t const* src,
                       size_t n,
                       byte_t* work,
                       byte_t* dst,
                       size_t chunk) {
  cobs_enc_ctx_t ctx;
  begin(&ctx, work, 255);
  size_t at = 0, out = 0;
  while (at < n) {
    size_t su = 0, du = 0;
    size_t const take = std::min(chunk, n - at);
    cobs_encode_inc_args_t const args{ src + at, dst + out, take, chunk };
    inc(&ctx, &args, &su, &du);
    out += du;
    if (!su && !du) {
      break;
    }
    at += su;
  }
  bool fin = false;
  while (!fin) {
    size_t du = 0;
    end(&ctx, dst + out, chunk, &du, &fin);
    out += du;
    if (!du && !fin) {
      break;
    }
  }
  return out;
}

void bench_encode_inc(config const& cfg, shape sh, size_t n, size_t chunk) {
  arena src(n), a(COBS_ENCODE_MAX(n) + 512), b(COBS_ENCODE_MAX(n) + 512);
  cobs_test::gen_shape(sh, src.at(0), n, 0xB0B0u);
  std::vector<byte_t> wa(255), wb(255);

  uint64_t const oa = drive_enc_inc(cobs_ref_encode_inc_begin,
                                    cobs_ref_encode_inc,
                                    cobs_ref_encode_inc_end,
                                    src.at(0),
                                    n,
                                    wa.data(),
                                    a.at(0),
                                    chunk);
  uint64_t const ob = drive_enc_inc(cobs_encode_inc_begin,
                                    cobs_encode_inc,
                                    cobs_encode_inc_end,
                                    src.at(0),
                                    n,
                                    wb.data(),
                                    b.at(0),
                                    chunk);

  auto ref = [&] {
    sink(drive_enc_inc(cobs_ref_encode_inc_begin,
                       cobs_ref_encode_inc,
                       cobs_ref_encode_inc_end,
                       src.at(0),
                       n,
                       wa.data(),
                       a.at(0),
                       chunk));
  };
  auto swar = [&] {
    sink(drive_enc_inc(cobs_encode_inc_begin,
                       cobs_encode_inc,
                       cobs_encode_inc_end,
                       src.at(0),
                       n,
                       wb.data(),
                       b.at(0),
                       chunk));
  };
  measure(cfg,
          "encode_inc",
          sh,
          n,
          0,
          0,
          chunk,
          cobs_test::mean_run_len(src.at(0), n),
          fnv(oa, a.at(0), static_cast<size_t>(oa)),
          fnv(ob, b.at(0), static_cast<size_t>(ob)),
          ref,
          swar);
}

template <typename BeginF, typename IncF>
uint64_t drive_dec_inc(BeginF begin,
                       IncF inc,
                       byte_t const* enc,
                       size_t n,
                       byte_t* dst,
                       size_t chunk) {
  cobs_decode_inc_ctx_t ctx;
  begin(&ctx);
  size_t at = 0, out = 0;
  bool done = false;
  while (!done && (at < n)) {
    size_t su = 0, du = 0;
    size_t const take = std::min(chunk, n - at);
    cobs_decode_inc_args_t const args{ enc + at, dst + out, take, chunk };
    inc(&ctx, &args, &su, &du, &done);
    out += du;
    if (!su && !du) {
      break;
    }
    at += su;
  }
  return out;
}

void bench_decode_inc(config const& cfg, shape sh, size_t n, size_t chunk) {
  arena raw(n);
  cobs_test::gen_shape(sh, raw.at(0), n, 0xB0B0u);
  std::vector<byte_t> enc(COBS_ENCODE_MAX(n));
  size_t enc_len = 0;
  byte_t dummy = 0;
  cobs_ref_encode(n ? raw.at(0) : &dummy, n, enc.data(), enc.size(), &enc_len);

  arena a(n + 512), b(n + 512);
  uint64_t const oa = drive_dec_inc(cobs_ref_decode_inc_begin,
                                    cobs_ref_decode_inc,
                                    enc.data(),
                                    enc_len,
                                    a.at(0),
                                    chunk);
  uint64_t const ob = drive_dec_inc(cobs_decode_inc_begin,
                                    cobs_decode_inc,
                                    enc.data(),
                                    enc_len,
                                    b.at(0),
                                    chunk);
  auto ref = [&] {
    sink(drive_dec_inc(cobs_ref_decode_inc_begin,
                       cobs_ref_decode_inc,
                       enc.data(),
                       enc_len,
                       a.at(0),
                       chunk));
  };
  auto swar = [&] {
    sink(drive_dec_inc(cobs_decode_inc_begin,
                       cobs_decode_inc,
                       enc.data(),
                       enc_len,
                       b.at(0),
                       chunk));
  };
  measure(cfg,
          "decode_inc",
          sh,
          n,
          0,
          0,
          chunk,
          cobs_test::mean_run_len(raw.at(0), n),
          fnv(oa, a.at(0), static_cast<size_t>(oa)),
          fnv(ob, b.at(0), static_cast<size_t>(ob)),
          ref,
          swar);
}

}  // namespace

int main(int argc, char** argv) {
  config cfg;
  bool quick = false;
  // --quick is scanned first so that an explicit --samples / --min-batch-ms
  // still wins no matter where it appears on the command line.
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--quick") {
      quick = true;
      cfg.samples = 5;
      cfg.batch_ns = 10'000'000;
    }
  }
  for (int i = 1; i < argc; ++i) {
    std::string const a = argv[i];
    if (a == "--quick") {
      quick = true;
    } else if (a.rfind("--filter=", 0) == 0) {
      cfg.filter = a.substr(9);
    } else if (a.rfind("--samples=", 0) == 0) {
      cfg.samples = std::atoi(a.c_str() + 10);
    } else if (a.rfind("--min-batch-ms=", 0) == 0) {
      cfg.batch_ns = static_cast<uint64_t>(std::atoi(a.c_str() + 15)) * 1000000ull;
    } else if (a.rfind("--cpu-ghz=", 0) == 0) {
      cfg.cpu_ghz = std::atof(a.c_str() + 10);
    } else {
      std::fprintf(stderr,
                   "usage: cobs_bench [--quick] [--filter=SUBSTR] [--samples=N]\n"
                   "                  [--min-batch-ms=N] [--cpu-ghz=F]\n");
      return 2;
    }
  }
  spin_warmup(200);
  uint64_t const res = clock_resolution_ns();
  uint64_t const ovh = clock_overhead_ns();

  std::printf("# nanocobs bench\n");
  std::printf("# rev\t%s\n", COBS_BENCH_REV);
  std::printf("# word\t%zu bytes (sizeof(size_t) on this target)\n", sizeof(size_t));
  std::printf("# clock\tsteady_clock measured_res=%lluns now()=%lluns\n",
              static_cast<unsigned long long>(res),
              static_cast<unsigned long long>(ovh));
  std::printf(
      "# policy\tmin-of-%d, batch>=%llums, hot buffers, A/B interleaved+alternated\n",
      cfg.samples,
      static_cast<unsigned long long>(cfg.batch_ns / 1000000ull));
  std::printf(
      "# note\tCOBS shifts dst relative to src by one byte per block, so relative\n"
      "# note\talignment drifts as encoding proceeds: there is no alignment-matched\n"
      "# note\tfast path. The offset sweep exists to prove the absence of a\n"
      "# note\tpathological penalty, not to find a fast one.\n");

  shape const shapes[] = {
    shape::nonzero,        shape::all_zero,     shape::bernoulli_p254,
    shape::bernoulli_1,    shape::bernoulli_5,  shape::bernoulli_25,
    shape::bernoulli_50,   shape::zero_every_9, shape::zero_every_17,
    shape::zero_every_254, shape::clustered,    shape::text,
    shape::zero_run_32,    shape::zero_run_8,   shape::sparse
  };
  size_t const lens_full[] = { 8,   16,  24,   32,   48,    64,    96,      128,
                               256, 512, 1024, 4096, 16384, 65536, 1048576, 4194304 };
  size_t const lens_quick[] = { 16, 64, 256, 4096, 65536 };

  size_t const* lens = quick ? lens_quick : lens_full;
  size_t const n_lens = quick ? (sizeof(lens_quick) / sizeof(lens_quick[0]))
                              : (sizeof(lens_full) / sizeof(lens_full[0]));

  for (shape const sh : shapes) {
    for (size_t li = 0; li < n_lens; ++li) {
      bench_encode(cfg, sh, lens[li], 0, 0);
      bench_decode(cfg, sh, lens[li], 0, 0);
    }
  }

  if (!quick) {
    for (size_t a = 0; a < 8; ++a) {
      for (size_t b = 0; b < 8; ++b) {
        bench_encode(cfg, shape::bernoulli_p254, 65536, a, b);
        bench_decode(cfg, shape::bernoulli_p254, 65536, a, b);
      }
    }
  }

  // Tinyframe is bounded by COBS_TINYFRAME_SAFE_BUFFER_SIZE by construction.
  for (shape const sh : { shape::nonzero, shape::bernoulli_5, shape::all_zero }) {
    for (size_t p : { size_t{ 16 }, size_t{ 64 }, size_t{ 254 } }) {
      for (size_t ofs = 0; ofs < (quick ? 1u : 8u); ++ofs) {
        bench_tinyframe(cfg, sh, p, ofs, true);
        bench_tinyframe(cfg, sh, p, ofs, false);
      }
    }
  }

  // Incremental: the deliverable is the chunk size at which streaming approaches
  // one-shot, and whether SWAR makes small-chunk streaming slower than it is now.
  for (shape const sh : { shape::nonzero, shape::bernoulli_p254, shape::bernoulli_5 }) {
    for (size_t n : { size_t{ 1024 }, size_t{ 65536 } }) {
      for (size_t chunk :
           { size_t{ 16 }, size_t{ 64 }, size_t{ 255 }, size_t{ 256 }, size_t{ 1024 } }) {
        bench_encode_inc(cfg, sh, n, chunk);
        bench_decode_inc(cfg, sh, n, chunk);
      }
    }
  }

  print_rows(cfg);
  print_summary();
  return 0;
}
