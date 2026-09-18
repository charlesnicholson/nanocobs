// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_swar_runs.cc. Runs are what the lanes key on: one copies a
// data run, the other emits empty blocks. Every length from 1 to past a 0xFF block.

import { test } from 'node:test';
import { runOf, concat, u8 } from './helpers.mjs';
import { diffAll } from './differential.mjs';

const DATA = 0x41;

test('Runs: zeroes, 1 to 256 bytes', () => {
  for (let n = 1; n <= 256; ++n) diffAll(runOf(n, 0x00), `zero run ${n}`);
});

test('Runs: nonzeroes, 1 to 256 bytes', () => {
  for (let n = 1; n <= 256; ++n) {
    diffAll(runOf(n, DATA), `data run ${n}`);
    if (n <= 64) {
      // A run whose bytes all differ, so a stuck lane shows up.
      diffAll(Uint8Array.from({ length: n }, (_, i) => 1 + (i % 255)), `ascending ${n}`);
    }
  }
});

test('Runs: zeroes then data, both across every boundary', () => {
  for (const z of [1, 7, 8, 9, 15, 16, 17, 253, 254, 255, 256]) {
    for (const d of [1, 7, 8, 9, 16, 17, 253, 254, 255]) {
      diffAll(concat(runOf(z, 0x00), runOf(d, DATA)), `z${z}+d${d}`);
    }
  }
});

test('Runs: data then zeroes, both across every boundary', () => {
  for (const d of [1, 7, 8, 9, 15, 16, 17, 253, 254, 255, 256]) {
    for (const z of [1, 7, 8, 9, 16, 17, 253, 254, 255]) {
      diffAll(concat(runOf(d, DATA), runOf(z, 0x00)), `d${d}+z${z}`);
    }
  }
});

test('Runs: data sandwiched in zeroes, and zeroes sandwiched in data', () => {
  for (const outer of [1, 7, 8, 9, 16, 17, 254, 255]) {
    for (const inner of [1, 7, 8, 9, 16, 17, 254, 255]) {
      diffAll(concat(runOf(outer, 0x00), runOf(inner, DATA), runOf(outer, 0x00)),
              `z${outer}+d${inner}+z${outer}`);
      diffAll(concat(runOf(outer, DATA), runOf(inner, 0x00), runOf(outer, DATA)),
              `d${outer}+z${inner}+d${outer}`);
    }
  }
});

test('Runs: alternating, every period 1 to 40', () => {
  for (let period = 1; period <= 40; ++period) {
    for (let reps = 1; reps <= 6; ++reps) {
      const parts = [];
      for (let r = 0; r < reps; ++r) parts.push(runOf(period, DATA), runOf(period, 0x00));
      const v = concat(...parts);
      diffAll(v, `alternating p${period} r${reps}`);
      diffAll(concat(v, u8(DATA)), `alternating p${period} r${reps} odd tail`);
    }
  }
});

test('Runs: a 0xFF block boundary at every offset into a zero run', () => {
  // 254 data bytes force a 0xFF block; leading zeroes move the break to every phase.
  for (let lead = 0; lead <= 40; ++lead) {
    for (const run of [252, 253, 254, 255, 256, 507, 508, 509]) {
      diffAll(concat(runOf(lead, 0x00), runOf(run, DATA), runOf(lead, 0x00)),
              `lead${lead} run${run}`);
    }
  }
});

test('Runs: payloads of 0x01, the byte the decode lane pattern-matches on', () => {
  // The decoder's zero-run lane looks for 0x01 code bytes, and 0x01 is also an
  // ordinary payload byte. The two must not be confused.
  for (let n = 1; n <= 600; ++n) diffAll(runOf(n, 0x01), `0x01 run ${n}`);
  for (let lead = 0; lead <= 24; ++lead) {
    for (const n of [1, 7, 8, 9, 16, 17, 253, 254, 255, 256]) {
      diffAll(concat(runOf(lead, 0x00), runOf(n, 0x01), runOf(lead, 0x00)),
              `0x01 run ${n} between zero runs ${lead}`);
    }
  }
});

test('Runs: alternating 0x00 and 0x01, every length to 600', () => {
  for (let n = 1; n <= 600; ++n) {
    diffAll(Uint8Array.from({ length: n }, (_, i) => i & 1), `0/1 alt ${n}`);
    diffAll(Uint8Array.from({ length: n }, (_, i) => (i & 1) ^ 1), `1/0 alt ${n}`);
  }
});

test('Runs: a forced 0xFF block ending exactly on a zero', () => {
  // 254 bytes fill a block; the next byte decides whether a fresh code byte is
  // emitted or the delimiter stands in.
  for (const data of [252, 253, 254, 255, 256, 507, 508, 509, 510]) {
    for (let zeros = 0; zeros <= 8; ++zeros) {
      const v = concat(runOf(data, DATA), runOf(zeros, 0x00));
      diffAll(v, `d${data}+z${zeros}`);
      diffAll(concat(v, u8(DATA)), `d${data}+z${zeros}+1`);
    }
  }
});
