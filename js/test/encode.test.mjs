// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_cobs_encode.cc. Subcases become subtests. The C null-pointer
// cases become wrong-type or too-small ones; the wasm boundary makes null unreachable.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { u8, runOf, iota, concat, verifyFrameInvariants, encodeOk } from './helpers.mjs';

test('Encoding validation', async (t) => {
  await t.test('Bad argument types stand in for the C null-pointer cases', () => {
    for (const bad of [null, undefined, [1, 2], 'abc', new Int8Array(4)]) {
      assert.throws(() => cobs.encode(bad), TypeError);
      assert.throws(() => cobs.encodeInto(bad, new Uint8Array(8)), TypeError);
      assert.throws(() => cobs.encodeInto(u8(1), bad), TypeError);
    }
  });

  await t.test('Invalid enc_max', () => {
    const dec = runOf(32, 0x00);
    // cobs.c:214 -- enc_max < 2 is BAD_ARG, not EXHAUSTED.
    assert.equal(cobs.tryEncodeInto(dec, new Uint8Array(0)), -1);
    assert.equal(cobs.tryEncodeInto(dec, new Uint8Array(1)), -1);
    assert.equal(cobs.tryEncodeInto(dec, new Uint8Array(30)), -3);
    assert.equal(cobs.tryEncodeInto(dec, new Uint8Array(31)), -3);
  });

  await t.test('enc_max exactly sufficient', () => {
    const dec = runOf(4, 0x42);
    const needed = cobs.encodeMax(4);
    const frame = new Uint8Array(needed);
    assert.equal(cobs.encodeInto(dec, frame), 6);
  });

  await t.test('enc_max one byte short', () => {
    const dec = runOf(4, 0x42);
    assert.equal(cobs.tryEncodeInto(dec, new Uint8Array(cobs.encodeMax(4) - 1)), -3);
  });
});

test('Simple encodings', async (t) => {
  const cases = [
    ['Empty', u8(), u8(0x01, 0x00)],
    ['1 nonzero byte', u8(0x34), u8(0x02, 0x34, 0x00)],
    ['2 nonzero bytes', u8(0x34, 0x56), u8(0x03, 0x34, 0x56, 0x00)],
    ['8 nonzero bytes', u8(0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF),
      u8(0x09, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF, 0x00)],
    ['1 zero byte', u8(0x00), u8(0x01, 0x01, 0x00)],
    ['2 zero bytes', u8(0x00, 0x00), u8(0x01, 0x01, 0x01, 0x00)],
    ['8 zero bytes', runOf(8, 0x00), concat(runOf(9, 0x01), u8(0x00))],
    ['4 alternating zero/nonzero', u8(0x00, 0x11, 0x00, 0x22),
      u8(0x01, 0x02, 0x11, 0x02, 0x22, 0x00)],
    ['4 alternating nonzero/zero', u8(0x11, 0x00, 0x22, 0x00),
      u8(0x02, 0x11, 0x02, 0x22, 0x01, 0x00)],
  ];
  for (const [name, payload, expected] of cases) {
    await t.test(name, () => {
      assert.deepEqual(cobs.encode(payload), expected);
      verifyFrameInvariants(cobs.encode(payload), name);
    });
  }
});

test('Encoding 0xFF code blocks', async (t) => {
  await t.test('253 nonzero bytes (just under the 0xFF threshold)', () => {
    const frame = encodeOk(runOf(253, 0x42));
    assert.equal(frame[0], 0xFE);
    assert.deepEqual(cobs.decode(frame), runOf(253, 0x42));
  });

  await t.test('Exactly 254 nonzero bytes (single 0xFF block)', () => {
    const expected = concat(u8(0xFF), runOf(254, 0x01), u8(0x00));
    assert.deepEqual(cobs.encode(runOf(254, 0x01)), expected);
  });

  await t.test('255 nonzero bytes (two code blocks)', () => {
    const expected = concat(u8(0xFF), runOf(254, 0x01), u8(0x02, 0x01, 0x00));
    assert.deepEqual(cobs.encode(runOf(255, 0x01)), expected);
  });

  await t.test('256 nonzero bytes (0xFF block + 2)', () => {
    const frame = encodeOk(runOf(256, 0x01));
    assert.equal(frame[0], 0xFF);
    assert.deepEqual(cobs.decode(frame), runOf(256, 0x01));
  });

  await t.test('508 nonzero bytes (two full 0xFF blocks)', () => {
    const expected = concat(u8(0xFF), runOf(254, 0xAA), u8(0xFF), runOf(254, 0xAA), u8(0x00));
    assert.deepEqual(cobs.encode(runOf(508, 0xAA)), expected);
  });

  await t.test('254 nonzero bytes followed by zero', () => {
    const dec = concat(runOf(254, 0x01), u8(0x00));
    assert.deepEqual(cobs.decode(encodeOk(dec)), dec);
  });
});

// The nine examples from the Wikipedia article, shared with decode.test.mjs.
export const WIKIPEDIA = [
  ['single zero byte', u8(0x00), u8(0x01, 0x01, 0x00)],
  ['two zero bytes', u8(0x00, 0x00), u8(0x01, 0x01, 0x01, 0x00)],
  ['0x00 0x11 0x00', u8(0x00, 0x11, 0x00), u8(0x01, 0x02, 0x11, 0x01, 0x00)],
  ['0x11 0x22 0x00 0x33', u8(0x11, 0x22, 0x00, 0x33), u8(0x03, 0x11, 0x22, 0x02, 0x33, 0x00)],
  ['0x11 0x22 0x33 0x44', u8(0x11, 0x22, 0x33, 0x44), u8(0x05, 0x11, 0x22, 0x33, 0x44, 0x00)],
  ['0x11 0x00 0x00 0x00', u8(0x11, 0x00, 0x00, 0x00), u8(0x02, 0x11, 0x01, 0x01, 0x01, 0x00)],
  ['01..FE (254 nonzero bytes)', iota(254, 0x01),
    concat(u8(0xFF), iota(254, 0x01), u8(0x00))],
  ['00 01..FE (255 bytes starting with zero)', iota(255, 0x00),
    concat(u8(0x01, 0xFF), iota(254, 0x01), u8(0x00))],
  ['01..FF (255 nonzero bytes)', iota(255, 0x01),
    concat(u8(0xFF), iota(254, 0x01), u8(0x02, 0xFF, 0x00))],
];

test('Encode: Wikipedia examples', async (t) => {
  for (const [name, payload, frame] of WIKIPEDIA) {
    await t.test(name, () => assert.deepEqual(cobs.encode(payload), frame));
  }
});

test('Encode: known vectors from the COBS paper', async (t) => {
  await t.test('Figure 3: IP header fragment', () => {
    const dec = u8(0x45, 0x00, 0x00, 0x2C, 0x4C, 0x79, 0x00, 0x00, 0x40, 0x06, 0x4F, 0x37);
    const expected = u8(0x02, 0x45, 0x01, 0x04, 0x2C, 0x4C, 0x79,
                        0x01, 0x05, 0x40, 0x06, 0x4F, 0x37, 0x00);
    assert.deepEqual(cobs.encode(dec), expected);
  });
});

test('Longer payload encodings', async (t) => {
  await t.test('255 zero bytes', () => {
    assert.deepEqual(cobs.encode(runOf(255, 0x00)), concat(runOf(256, 0x01), u8(0x00)));
  });

  await t.test('1024 nonzero bytes', () => {
    const parts = [];
    for (let i = 0; i < Math.floor(1024 / 254); ++i) {
      parts.push(u8(0xFF), runOf(254, 0x21));
    }
    parts.push(u8((1024 % 254) + 1), runOf(1024 % 254, 0x21), u8(0x00));
    assert.deepEqual(cobs.encode(runOf(1024, 0x21)), concat(...parts));
  });

  await t.test('1024 zero bytes', () => {
    assert.deepEqual(cobs.encode(runOf(1024, 0x00)), concat(runOf(1025, 0x01), u8(0x00)));
  });

  await t.test('1024 every other byte is zero', () => {
    const dec = Uint8Array.from({ length: 1024 }, (_, i) => i & 1);
    const expected = concat(
      Uint8Array.from({ length: 1025 }, (_, i) => (i & 1) ? 2 : 1), u8(0x00));
    assert.deepEqual(cobs.encode(dec), expected);
  });

  await t.test('Ascending byte pattern', () => {
    const dec = iota(512, 0x00);
    assert.deepEqual(cobs.decode(encodeOk(dec)), dec);
  });
});

test('Encode: frame invariants', async (t) => {
  await t.test('Single byte values 0x00..0xFF', () => {
    for (let b = 0; b <= 0xFF; ++b) verifyFrameInvariants(cobs.encode(u8(b)), `byte ${b}`);
  });

  await t.test('Runs of identical bytes at boundary lengths', () => {
    for (const b of [0x00, 0x01, 0x7F, 0xFE, 0xFF]) {
      for (const len of [1, 2, 253, 254, 255, 256, 508, 509]) {
        verifyFrameInvariants(cobs.encode(runOf(len, b)), `${len}x${b}`);
      }
    }
  });
});

test('Encode: round-trip encode/decode', async (t) => {
  await t.test('Single byte values', () => {
    for (let b = 0; b <= 0xFF; ++b) {
      const dec = u8(b);
      assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
    }
  });

  await t.test('Two-byte combinations with zeros', () => {
    for (let b = 0; b <= 0xFF; ++b) {
      for (const dec of [u8(0x00, b), u8(b, 0x00)]) {
        assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
      }
    }
  });

  await t.test('Runs at boundary lengths', () => {
    for (const b of [0x00, 0x01, 0xFF]) {
      for (const len of [1, 253, 254, 255, 256, 508, 509, 1000]) {
        const dec = runOf(len, b);
        assert.deepEqual(cobs.decode(cobs.encode(dec)), dec, `${len}x${b}`);
      }
    }
  });

  await t.test('Ascending byte patterns at boundary lengths', () => {
    for (const len of [1, 253, 254, 255, 256, 508, 512, 1024]) {
      const dec = iota(len, 0x00);
      assert.deepEqual(cobs.decode(cobs.encode(dec)), dec, `len ${len}`);
    }
  });

  await t.test('Zero at every Nth position', () => {
    for (const n of [1, 2, 127, 253, 254, 255]) {
      const dec = runOf(1024, 0x42);
      for (let i = 0; i < dec.length; i += n) dec[i] = 0x00;
      assert.deepEqual(cobs.decode(cobs.encode(dec)), dec, `every ${n}`);
    }
  });
});
