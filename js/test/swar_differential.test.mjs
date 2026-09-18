// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_swar_differential.cc. The C diffs SWAR against a byte-loop
// build of the same source; this diffs the wasm against a JavaScript reference, which
// also catches wrapper bugs. The incremental cases are omitted.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { runOf, concat, u8 } from './helpers.mjs';
import { diffAll, diffEncode, diffDecode } from './differential.mjs';
import { refEncodeMax, refDecodeMax } from './cobs_ref.mjs';

// Only zero-vs-nonzero matters to COBS, so {0x00, 0x01} is a complete model.
// Length 0..16 covers every zero pattern within two 64-bit words.
test('SWAR differential: exhaustive binary alphabet, len 0..16', () => {
  for (let len = 0; len <= 16; ++len) {
    for (let m = 0; m < (1 << len); ++m) {
      const p = Uint8Array.from({ length: len }, (_, i) => ((m >> i) & 1) ? 0x01 : 0x00);
      diffAll(p, `len=${len} mask=0x${m.toString(16)}`);
    }
  }
});

// Three symbols separate zero, ordinary byte, and 0xFF, which the block limit
// keys on.
test('SWAR differential: exhaustive ternary alphabet, len 0..8', () => {
  const alpha = [0x00, 0x01, 0xFF];
  for (let len = 0; len <= 8; ++len) {
    const combos = 3 ** len;
    for (let m = 0; m < combos; ++m) {
      const p = new Uint8Array(len);
      let v = m;
      for (let i = 0; i < len; ++i) { p[i] = alpha[v % 3]; v = Math.floor(v / 3); }
      diffAll(p, `ternary len=${len} m=${m}`);
    }
  }
});

// The pairs catch the classic word-at-a-time bug: finding the first zero in a word
// and missing the second.
test('SWAR differential: single zero at every position', () => {
  for (let len = 1; len <= 80; ++len) {
    const p = runOf(len, 0xAA);
    for (let i = 0; i < len; ++i) {
      p[i] = 0x00;
      diffAll(p, `single zero len=${len} at=${i}`);
      p[i] = 0xAA;
    }
  }
});

test('SWAR differential: two zeros at every pair of positions', () => {
  for (let len = 2; len <= 40; ++len) {
    const p = runOf(len, 0xAA);
    for (let i = 0; i < len; ++i) {
      for (let j = i + 1; j < len; ++j) {
        p[i] = 0x00; p[j] = 0x00;
        diffAll(p, `two zeros len=${len} at=${i},${j}`);
        p[i] = 0xAA; p[j] = 0xAA;
      }
    }
  }
});

// The forced 0xFF break is the one thing a lane keys on a counter, not on data.
test('SWAR differential: long runs at every word phase', () => {
  for (const run of [252, 253, 254, 255, 256, 507, 508, 509, 510, 762, 763, 764]) {
    for (let phase = 0; phase <= 8; ++phase) {
      diffAll(concat(runOf(phase, 0x00), runOf(run, 0x55)), `run=${run} phase=${phase}`);
      diffAll(concat(runOf(phase, 0x55), runOf(run, 0x00)), `zrun=${run} phase=${phase}`);
    }
  }
});

// EXHAUSTED must fire at the same byte, and nothing may be written at or past the
// limit, which the harness's poisoned tail enforces.
test('SWAR differential: destination exhaustion sweep', () => {
  for (const len of [0, 1, 7, 8, 9, 15, 16, 17, 31, 63, 64, 65, 253, 254, 255, 256]) {
    for (let which = 0; which < 3; ++which) {
      let p = runOf(len, 0x77);
      if (which === 1) p = runOf(len, 0x00);
      else if (which === 2) {
        p = Uint8Array.from({ length: len }, (_, i) => ((i % 9) === 8) ? 0x00 : 0x55);
      }

      const exact = refEncodeMax(len);
      for (let emax = 0; emax <= exact + 2; ++emax) {
        diffEncode(p, emax, `encode which=${which}`);
      }

      const frame = cobs.encode(p);
      for (let dmax = 0; dmax <= refDecodeMax(frame.length) + 2; ++dmax) {
        diffDecode(frame, dmax, `decode which=${which}`);
      }
    }
  }
});

// Where a word-at-a-time validator diverges: the byte loop bails on the first
// embedded zero, while a lane may have written more first.
test('SWAR differential: single-byte mutations of valid frames', () => {
  const muts = [0x00, 0x01, 0x02, 0x7F, 0xFE, 0xFF];
  for (const len of [1, 5, 8, 9, 16, 17, 31, 64, 255, 260]) {
    const p = Uint8Array.from({ length: len }, (_, i) => 1 + (i % 254));
    const good = cobs.encode(p);
    for (let i = 0; i < good.length; ++i) {
      for (const mv of muts) {
        if (good[i] === mv) continue;
        const bad = Uint8Array.from(good);
        bad[i] = mv;
        diffDecode(bad, refDecodeMax(bad.length), `mutation len=${len} at=${i} to=${mv}`);
      }
    }
  }
});

test('SWAR differential: randomized', () => {
  // Seeded so a failure reproduces; the seed is printed in the assertion message.
  let s = 0x12345678;
  const rnd = () => { s ^= s << 13; s |= 0; s ^= s >>> 17; s ^= s << 5; s |= 0; return s >>> 0; };
  for (let iter = 0; iter < 4000; ++iter) {
    const len = rnd() % 700;
    const zeroPct = rnd() % 101;
    const p = Uint8Array.from({ length: len },
      () => ((rnd() % 100) < zeroPct) ? 0 : 1 + (rnd() % 255));
    diffAll(p, `random iter=${iter} len=${len} zero%=${zeroPct} seed=0x12345678`);
  }
});

test('SWAR differential: round trip at every length 0..600 across every shape', async () => {
  // From test_swar_boundaries.cc, the one case there on the one-shot API.
  const { SHAPES, makeShape } = await import('./shapes.mjs');
  for (const shape of SHAPES) {
    for (let n = 0; n <= 600; ++n) {
      diffAll(makeShape(shape, n, n + 1), `${shape} n=${n}`);
    }
  }
});
