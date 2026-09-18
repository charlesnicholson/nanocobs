// SPDX-License-Identifier: Unlicense OR 0BSD
//
// A port of tests/cobs_shapes.h, so the JS suite runs the same corpus as the C tests
// and the benchmark. Every shape moves one knob: the run length between zeros.
//
// The PRNG is not std::mt19937. These payloads are compared between two
// implementations, so only the statistics matter; exact bytes are the vectors' job.

function xorshift32(seed) {
  let s = seed | 0 || 0x9e3779b9;
  return () => {
    s ^= s << 13; s |= 0;
    s ^= s >>> 17;
    s ^= s << 5; s |= 0;
    return s >>> 0;
  };
}

export const SHAPES = [
  'nonzero', 'all_zero', 'z_1of254', 'z_1pct', 'z_5pct', 'z_25pct', 'z_50pct',
  'z_every_8', 'z_every_9', 'z_every_16', 'z_every_17', 'z_every_254',
  'z_every_255', 'clustered', 'text', 'zero_run_32', 'zero_run_8', 'sparse',
];

export function makeShape(shape, n, seed = 1) {
  const rng = xorshift32(seed);
  const out = new Uint8Array(n);
  const nz = () => 1 + (rng() % 255);
  const every = (k) => { for (let i = 0; i < n; ++i) out[i] = (i % k) === k - 1 ? 0 : nz(); };
  const bern = (p) => { for (let i = 0; i < n; ++i) out[i] = (rng() / 0x100000000) < p ? 0 : nz(); };

  switch (shape) {
    case 'nonzero':     for (let i = 0; i < n; ++i) out[i] = nz(); break;
    case 'all_zero':    break;                       // already zeroed
    case 'z_1of254':    bern(1 / 254); break;
    case 'z_1pct':      bern(0.01); break;
    case 'z_5pct':      bern(0.05); break;
    case 'z_25pct':     bern(0.25); break;
    case 'z_50pct':     bern(0.50); break;
    case 'z_every_8':   every(8); break;
    case 'z_every_9':   every(9); break;
    case 'z_every_16':  every(16); break;
    case 'z_every_17':  every(17); break;
    case 'z_every_254': every(254); break;
    case 'z_every_255': every(255); break;
    case 'text':        for (let i = 0; i < n; ++i) out[i] = 0x20 + (rng() % 95); break;
    case 'zero_run_32': for (let i = 0; i < n; ++i) out[i] = ((i >>> 5) & 1) ? 0 : nz(); break;
    case 'zero_run_8':  for (let i = 0; i < n; ++i) out[i] = ((i >>> 3) & 1) ? 0 : nz(); break;
    case 'clustered': {
      let i = 0;
      while (i < n) {
        const run = 1024 + (rng() % 3072);
        for (let k = 0; k < run && i < n; ++k, ++i) out[i] = nz();
        const burst = 1 + (rng() % 8);
        for (let k = 0; k < burst && i < n; ++k, ++i) out[i] = 0;
      }
      break;
    }
    case 'sparse': {
      let i = 0;
      while (i < n) {
        const run = 48 + (rng() % 208);
        for (let k = 0; k < run && i < n; ++k, ++i) out[i] = 0;
        const burst = 1 + (rng() % 12);
        for (let k = 0; k < burst && i < n; ++k, ++i) out[i] = nz();
      }
      break;
    }
    default: throw new Error(`unknown shape ${shape}`);
  }
  return out;
}

/** Mean payload bytes between zeros: the best single predictor of COBS cost. */
export function meanRunLen(p) {
  let zeros = 0;
  for (const b of p) if (!b) ++zeros;
  return zeros ? p.length / zeros : p.length;
}
