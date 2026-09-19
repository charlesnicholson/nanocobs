// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_cobs_decode.cc.
//
// No divergence: decode() is cobs_decode, including stopping at the first delimiter
// and ignoring anything after it. decodeFirst() adds only out_enc_consumed.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { u8, runOf, iota, concat } from './helpers.mjs';
import { WIKIPEDIA } from './encode.test.mjs';

test('Decoding validation', async (t) => {
  await t.test('Bad argument types stand in for the C null-pointer cases', () => {
    for (const bad of [null, undefined, [1, 0], 'ab', new Int8Array(2)]) {
      assert.throws(() => cobs.decode(bad), TypeError);
      assert.throws(() => cobs.decodeInto(bad, new Uint8Array(8)), TypeError);
      assert.throws(() => cobs.decodeInto(u8(0x01, 0x00), bad), TypeError);
    }
  });

  await t.test('Invalid enc_len', () => {
    // cobs.c:509 -- enc_len < 2 is BAD_ARG.
    assert.throws(() => cobs.decode(u8()), (e) => e.code === 'BAD_ARG');
    // A single 0x00 has its delimiter in the right place, so it reaches the C.
    assert.throws(() => cobs.decode(u8(0x00)), (e) => e.code === 'BAD_ARG');
  });

  await t.test('Invalid payload: code byte jumps past end', () => {
    assert.throws(() => cobs.decode(u8(3, 0)), (e) => e.code === 'EXHAUSTED');
  });

  await t.test('Invalid payload: code byte jumps over internal zeroes', () => {
    assert.throws(() => cobs.decode(u8(5, 1, 0, 0, 1, 0)), (e) => e.code === 'BAD_PAYLOAD');
  });

  await t.test('Invalid payload: embedded zero in run', () => {
    assert.throws(() => cobs.decode(u8(0x04, 0x01, 0x00, 0x03, 0x00)),
                  (e) => e.code === 'BAD_PAYLOAD');
  });

  await t.test('Output buffer too small', () => {
    const frame = u8(0x05, 0x11, 0x22, 0x33, 0x44, 0x00);
    assert.equal(cobs.tryDecodeInto(frame, new Uint8Array(2)), -3);
  });

  await t.test('Output buffer exactly right', () => {
    const frame = u8(0x05, 0x11, 0x22, 0x33, 0x44, 0x00);
    const out = new Uint8Array(4);
    assert.equal(cobs.decodeInto(frame, out), 4);
    assert.deepEqual(out, u8(0x11, 0x22, 0x33, 0x44));
  });

  await t.test('Missing trailing delimiter', () => {
    // No delimiter anywhere in the buffer: the decoder runs out of input.
    assert.throws(() => cobs.decode(u8(0x02, 0x01)), (e) => e.code === 'EXHAUSTED');
  });
});

test('Simple decodings', async (t) => {
  const cases = [
    ['Empty payload', u8(0x01, 0x00), u8()],
    ['1 nonzero byte', u8(0x02, 0x34, 0x00), u8(0x34)],
    ['2 nonzero bytes', u8(0x03, 0x34, 0x56, 0x00), u8(0x34, 0x56)],
    ['8 nonzero bytes', u8(0x09, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF, 0x00),
      u8(0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF)],
    ['1 zero byte', u8(0x01, 0x01, 0x00), u8(0x00)],
    ['2 zero bytes', u8(0x01, 0x01, 0x01, 0x00), u8(0x00, 0x00)],
    ['8 zero bytes', concat(runOf(9, 0x01), u8(0x00)), runOf(8, 0x00)],
    ['4 alternating zero/nonzero', u8(0x01, 0x02, 0x11, 0x02, 0x22, 0x00),
      u8(0x00, 0x11, 0x00, 0x22)],
    ['4 alternating nonzero/zero', u8(0x02, 0x11, 0x02, 0x22, 0x01, 0x00),
      u8(0x11, 0x00, 0x22, 0x00)],
  ];
  for (const [name, frame, expected] of cases) {
    await t.test(name, () => assert.deepEqual(cobs.decode(frame), expected));
  }
});

test('Decoding 0xFF code blocks', async (t) => {
  await t.test('Exactly 254 nonzero bytes', () => {
    const frame = concat(u8(0xFF), runOf(254, 0x01), u8(0x00));
    assert.deepEqual(cobs.decode(frame), runOf(254, 0x01));
  });

  await t.test('255 nonzero bytes (two code blocks)', () => {
    const frame = concat(u8(0xFF), runOf(254, 0x01), u8(0x02, 0x01, 0x00));
    assert.deepEqual(cobs.decode(frame), runOf(255, 0x01));
  });

  await t.test('508 nonzero bytes (two full 0xFF blocks)', () => {
    const frame = concat(u8(0xFF), runOf(254, 0xAA), u8(0xFF), runOf(254, 0xAA), u8(0x00));
    assert.deepEqual(cobs.decode(frame), runOf(508, 0xAA));
  });

  await t.test('254 nonzero bytes followed by zero', () => {
    const dec = concat(runOf(254, 0x01), u8(0x00));
    assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
  });
});

test('Decode: known vectors from the COBS paper', async (t) => {
  await t.test('Figure 3: IP header fragment', () => {
    const frame = u8(0x02, 0x45, 0x01, 0x04, 0x2C, 0x4C, 0x79,
                     0x01, 0x05, 0x40, 0x06, 0x4F, 0x37, 0x00);
    const expected = u8(0x45, 0x00, 0x00, 0x2C, 0x4C, 0x79,
                        0x00, 0x00, 0x40, 0x06, 0x4F, 0x37);
    assert.deepEqual(cobs.decode(frame), expected);
  });
});

test('Decode: Wikipedia examples', async (t) => {
  for (const [name, payload, frame] of WIKIPEDIA) {
    await t.test(name, () => assert.deepEqual(cobs.decode(frame), payload));
  }
});

test('Wikipedia round-trip examples', async (t) => {
  for (const [name, payload, frame] of WIKIPEDIA) {
    await t.test(name, () => {
      assert.deepEqual(cobs.encode(payload), frame);
      assert.deepEqual(cobs.decode(frame), payload);
      assert.deepEqual(cobs.decode(cobs.encode(payload)), payload);
    });
  }
});

test('Decode: longer payloads', async (t) => {
  await t.test('255 zero bytes', () => {
    assert.deepEqual(cobs.decode(concat(runOf(256, 0x01), u8(0x00))), runOf(255, 0x00));
  });

  await t.test('1024 nonzero bytes', () => {
    const dec = runOf(1024, 0x21);
    assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
  });

  await t.test('1024 zero bytes', () => {
    assert.deepEqual(cobs.decode(concat(runOf(1025, 0x01), u8(0x00))), runOf(1024, 0x00));
  });

  await t.test('1024 alternating zero/nonzero', () => {
    const dec = Uint8Array.from({ length: 1024 }, (_, i) => i & 1);
    assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
  });
});

test('Decode: encode/decode round-trips', async (t) => {
  await t.test('Single byte values 0x00..0xFF', () => {
    for (let b = 0; b <= 0xFF; ++b) {
      assert.deepEqual(cobs.decode(cobs.encode(u8(b))), u8(b));
    }
  });

  await t.test('Two-byte combinations with zeros', () => {
    for (let b = 0; b <= 0xFF; ++b) {
      for (const dec of [u8(0x00, b), u8(b, 0x00)]) {
        assert.deepEqual(cobs.decode(cobs.encode(dec)), dec);
      }
    }
  });

  await t.test('Runs of identical bytes', () => {
    for (const b of [0x00, 0x01, 0x7F, 0xFE, 0xFF]) {
      for (const len of [1, 2, 253, 254, 255, 256, 508, 509, 1000]) {
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
});
