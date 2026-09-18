// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_many_random_payloads.cc. The incremental and tinyframe
// cases are omitted; the package does not expose them.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { diffAll } from './differential.mjs';
import { SHAPES, makeShape } from './shapes.mjs';

function rng(seed) {
  let s = seed | 0 || 0x9e3779b9;
  return () => { s ^= s << 13; s |= 0; s ^= s >>> 17; s ^= s << 5; s |= 0; return s >>> 0; };
}

test('many random payloads', () => {
  const r = rng(0xC0B5E1);
  for (let i = 0; i < 3000; ++i) {
    const len = r() % 2048;
    const zeroPct = r() % 101;
    const p = Uint8Array.from({ length: len },
      () => ((r() % 100) < zeroPct) ? 0 : 1 + (r() % 255));
    assert.deepEqual(cobs.decode(cobs.encode(p)), p,
      `iter=${i} len=${len} zero%=${zeroPct}`);
  }
});

test('random payloads near code-block boundaries', () => {
  // The encoder's 0xFF path lives at the 254-byte limit, so cluster around it.
  const r = rng(0xB10C4);
  for (const base of [254, 508, 762, 1016]) {
    for (let d = -4; d <= 4; ++d) {
      const len = base + d;
      for (let iter = 0; iter < 40; ++iter) {
        const zeroPct = r() % 101;
        const p = Uint8Array.from({ length: len },
          () => ((r() % 100) < zeroPct) ? 0 : 1 + (r() % 255));
        diffAll(p, `base=${base} d=${d} iter=${iter}`);
      }
    }
  }
});

test('every shape at a spread of lengths round-trips', () => {
  for (const shape of SHAPES) {
    for (const n of [0, 1, 8, 9, 254, 255, 256, 1024, 3072, 20480]) {
      const p = makeShape(shape, n, n + 7);
      assert.deepEqual(cobs.decode(cobs.encode(p)), p, `${shape} n=${n}`);
    }
  }
});
