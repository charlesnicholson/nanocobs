// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_cobs_encode_max.cc. The C cross-checks the macro against a
// C function; here it is checked against the lengths encode actually produces.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { runOf } from './helpers.mjs';

test('encodeMax', async (t) => {
  await t.test('0, 1, 2 bytes', () => {
    assert.equal(cobs.encodeMax(0), 2);
    assert.equal(cobs.encodeMax(1), 3);
    assert.equal(cobs.encodeMax(2), 4);
  });

  await t.test('3-254 bytes (overhead = 2)', () => {
    for (let i = 3; i <= 254; ++i) assert.equal(cobs.encodeMax(i), i + 2, `n=${i}`);
  });

  await t.test('255-508 bytes (overhead = 3)', () => {
    for (let i = 255; i <= 508; ++i) assert.equal(cobs.encodeMax(i), i + 3, `n=${i}`);
  });

  await t.test('509-762 bytes (overhead = 4)', () => {
    for (let i = 509; i <= 762; ++i) assert.equal(cobs.encodeMax(i), i + 4, `n=${i}`);
  });

  await t.test('Boundary values', () => {
    assert.equal(cobs.encodeMax(254), 256);
    assert.equal(cobs.encodeMax(255), 258);
    assert.equal(cobs.encodeMax(508), 511);
    assert.equal(cobs.encodeMax(509), 513);
    assert.equal(cobs.encodeMax(762), 766);
    assert.equal(cobs.encodeMax(763), 768);
  });

  await t.test('Formula: 1 + n + ceil(n/254) for n > 0', () => {
    for (let n = 1; n <= 2048; ++n) {
      assert.equal(cobs.encodeMax(n), 1 + n + Math.floor((n + 253) / 254), `n=${n}`);
    }
  });

  await t.test('Monotonically increasing', () => {
    for (let n = 0; n < 2048; ++n) assert.ok(cobs.encodeMax(n) < cobs.encodeMax(n + 1));
  });

  await t.test('Always at least n + 2', () => {
    for (let n = 0; n <= 2048; ++n) assert.ok(cobs.encodeMax(n) >= n + 2, `n=${n}`);
  });

  await t.test('Large values', () => {
    for (const n of [12345, 65535, 1000000]) {
      assert.equal(cobs.encodeMax(n), 1 + n + Math.floor((n + 253) / 254), `n=${n}`);
    }
  });

  await t.test('It really is an upper bound on what encode produces', () => {
    // It is only useful if it never under-promises. Worst case is all-zero input.
    for (const n of [0, 1, 2, 253, 254, 255, 256, 508, 509, 1024]) {
      for (const fill of [0x00, 0x01, 0xFF]) {
        const frame = cobs.encode(runOf(n, fill));
        assert.ok(frame.length <= cobs.encodeMax(n), `n=${n} fill=${fill}`);
      }
    }
  });

  await t.test('decodeMax is an upper bound on what decode produces', () => {
    for (const n of [0, 1, 253, 254, 255, 256, 1024]) {
      for (const fill of [0x00, 0x01, 0xFF]) {
        const frame = cobs.encode(runOf(n, fill));
        assert.ok(cobs.decode(frame).length <= cobs.decodeMax(frame.length));
      }
    }
  });
});
