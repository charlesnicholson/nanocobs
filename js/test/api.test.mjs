// SPDX-License-Identifier: Unlicense OR 0BSD

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';

const u8 = (...b) => new Uint8Array(b);
const ramp = (n, f = i => (i % 255) + 1) => Uint8Array.from({ length: n }, (_, i) => f(i));

test('COBS_ENCODE_MAX mirrors the C macro', () => {
  assert.equal(cobs.encodeMax(0), 2);
  assert.equal(cobs.encodeMax(1), 3);
  assert.equal(cobs.encodeMax(254), 256);
  assert.equal(cobs.encodeMax(255), 258);
  for (let n = 1; n < 600; ++n) {
    assert.equal(cobs.encodeMax(n), 1 + n + Math.ceil(n / 254));
    assert.ok(cobs.encodeMax(n) >= n + 2);
  }
});

test('decodeMax is the proven bound', () => {
  assert.equal(cobs.decodeMax(0), 0);
  assert.equal(cobs.decodeMax(2), 0);
  assert.equal(cobs.decodeMax(3072), 3070);
});

test('vectors from the Cheshire/Baker paper and Wikipedia', () => {
  const vectors = [
    [u8(), u8(0x01, 0x00)],
    [u8(0x00), u8(0x01, 0x01, 0x00)],
    [u8(0x00, 0x00), u8(0x01, 0x01, 0x01, 0x00)],
    [u8(0x11, 0x22, 0x00, 0x33), u8(0x03, 0x11, 0x22, 0x02, 0x33, 0x00)],
    [u8(0x11, 0x00, 0x00, 0x00), u8(0x02, 0x11, 0x01, 0x01, 0x01, 0x00)],
    [u8(0x11, 0x22, 0x33, 0x44), u8(0x05, 0x11, 0x22, 0x33, 0x44, 0x00)],
  ];
  for (const [payload, frame] of vectors) {
    assert.deepEqual(cobs.encode(payload), frame);
    assert.deepEqual(cobs.decode(frame), payload);
  }
});

test('frames end with the delimiter and contain no other zero', () => {
  for (const n of [0, 1, 253, 254, 255, 256, 257, 508, 509, 510, 2048, 3072, 20480]) {
    const f = cobs.encode(ramp(n));
    assert.equal(f[f.length - 1], 0x00, `n=${n} must end with the delimiter`);
    assert.equal(f.indexOf(0), f.length - 1, `n=${n} must have no interior zero`);
    assert.ok(f.length <= cobs.encodeMax(n), `n=${n} exceeded encodeMax`);
  }
});

test('round-trips across the block boundaries and production sizes', () => {
  // 253-257 straddles the 254-byte block limit, where code == 0xFF lives.
  for (const n of [0, 1, 2, 7, 8, 9, 15, 16, 17, 253, 254, 255, 256, 257, 2048, 3072, 20480]) {
    for (const gen of [i => (i % 255) + 1, i => i % 256, () => 0, i => (i % 9 ? 1 : 0)]) {
      const p = ramp(n, gen);
      assert.deepEqual(cobs.decode(cobs.encode(p)), p, `n=${n}`);
    }
  }
});

test('encodeInto and decodeInto allocate nothing and report lengths', () => {
  const payload = u8(1, 2, 0, 3);
  const frame = new Uint8Array(cobs.encodeMax(payload.length));
  const n = cobs.encodeInto(payload, frame);
  assert.equal(n, 6);
  assert.deepEqual(frame.subarray(0, n), u8(0x03, 0x01, 0x02, 0x02, 0x03, 0x00));

  const out = new Uint8Array(cobs.decodeMax(n));
  assert.equal(cobs.decodeInto(frame.subarray(0, n), out), 4);
  assert.deepEqual(out.subarray(0, 4), payload);
});

test('try* forms return negated cobs_ret_t instead of throwing', () => {
  const out = new Uint8Array(8);
  assert.equal(cobs.tryDecodeInto(u8(0x09, 0x09), out), -3);       // input ran out
  assert.equal(cobs.tryEncodeInto(u8(1, 2, 3), new Uint8Array(1)), -1);  // cap < 2
  assert.ok(cobs.tryEncodeInto(u8(1, 2, 3), new Uint8Array(8)) > 0);
});

test('a leading 0x00 is decided by the bounds, exactly as cobs.c decides it', () => {
  // cobs.c has no zero test on the code byte: "while (block - 1)" wraps, so a frame
  // starting with 0x00 reads as an unbounded block, and whichever bound it reaches
  // first picks the error. Same frame, two destinations, two answers -- both pinned
  // because the package must report what the C reports, wart included.
  const frame = u8(0x00, 0x09, 0x00, 0x00);
  assert.equal(cobs.tryDecodeInto(frame, new Uint8Array(2)), -2);  // hits the zero
  assert.equal(cobs.tryDecodeInto(frame, new Uint8Array(1)), -3);  // fills dst first
});

test('encodeInto with a one-byte destination is BAD_ARG, not EXHAUSTED', () => {
  // cobs_encode rejects enc_max < 2 up front; larger but too small is EXHAUSTED.
  // Easy to get backwards, so pin it.
  assert.throws(() => cobs.encodeInto(u8(1, 2, 3), new Uint8Array(1)),
                (e) => e.code === 'BAD_ARG');
  assert.throws(() => cobs.encodeInto(ramp(64), new Uint8Array(4)),
                (e) => e.code === 'EXHAUSTED');
});

test('decode rejects malformed frames with the code the C returns', () => {
  // Every one of these is the raw cobs_decode answer: the package adds no rule of
  // its own on the way in.
  assert.throws(() => cobs.decode(u8()), (e) => e.code === 'BAD_ARG');
  assert.throws(() => cobs.decode(u8(0x01)), (e) => e.code === 'BAD_ARG');
  // Ran out of input before finding a delimiter.
  assert.throws(() => cobs.decode(u8(0x09, 0x09)), (e) => e.code === 'EXHAUSTED');
  // A well-delimited frame whose code byte points past its end.
  assert.throws(() => cobs.decode(u8(0x03, 0x00)), (e) => e.code === 'EXHAUSTED');
  // A code byte that jumps over an interior zero is still a malformed frame.
  assert.throws(() => cobs.decode(u8(0x04, 0x01, 0x00, 0x03, 0x00)),
                (e) => e.code === 'BAD_PAYLOAD');
});

test('decode stops at the first delimiter and ignores the rest', () => {
  // cobs_decode's contract (cobs.h:97): enc may hold more than one frame. decode()
  // hands back the first and drops the remainder; decodeFirst() is how a caller
  // learns where the next one starts.
  const two = new Uint8Array([...cobs.encode(u8(1, 2)), ...cobs.encode(u8(3, 4))]);
  assert.deepEqual(cobs.decode(two), u8(1, 2));
  const { payload, consumed } = cobs.decodeFirst(two);
  assert.deepEqual(payload, u8(1, 2));
  assert.deepEqual(cobs.decode(two.subarray(consumed)), u8(3, 4));
  // An empty first frame is still a frame, not a malformed buffer.
  assert.deepEqual(cobs.decode(u8(1, 0, 1, 0)), u8());
});

test('decodeInto stops at the first delimiter too', () => {
  const two = new Uint8Array([...cobs.encode(u8(1, 2)), ...cobs.encode(u8(3, 4))]);
  const out = new Uint8Array(16);
  assert.equal(cobs.decodeInto(two, out), 2);
  assert.deepEqual(out.subarray(0, 2), u8(1, 2));
});


test('CobsError carries code, ret, and a cross-realm-safe brand', () => {
  try {
    cobs.decode(u8(0x04, 0x01, 0x00, 0x03, 0x00));
    assert.fail('expected a throw');
  } catch (e) {
    assert.equal(e.name, 'CobsError');
    assert.equal(e.code, 'BAD_PAYLOAD');
    assert.equal(e.ret, 2);
    assert.ok(cobs.CobsError.isCobsError(e));
    assert.ok(!cobs.CobsError.isCobsError(new Error('nope')));
  }
});

test('non-Uint8Array arguments are a TypeError, not a CobsError', () => {
  for (const bad of [[1, 2], 'abc', null, undefined, new Int8Array(4), new DataView(new ArrayBuffer(4))]) {
    assert.throws(() => cobs.encode(bad), TypeError);
  }
});

test('a pooled Buffer with a nonzero byteOffset is handled correctly', () => {
  // The likeliest bug here: a pooled Buffer is a window into a shared slab, so an
  // internal new Uint8Array(src.buffer) would read the wrong bytes.
  const slab = Buffer.allocUnsafe(1024);
  const view = slab.subarray(300, 308);
  view.set([7, 8, 0, 9, 0, 0, 1, 2]);
  assert.ok(view.byteOffset > 0, 'expected a nonzero byteOffset for this test to bite');
  assert.deepEqual(cobs.decode(cobs.encode(view)), u8(7, 8, 0, 9, 0, 0, 1, 2));
});

test('a subarray view works as both source and destination', () => {
  const big = ramp(64);
  const src = big.subarray(8, 20);
  const dstBacking = new Uint8Array(128);
  const dst = dstBacking.subarray(16, 16 + cobs.encodeMax(src.length));
  const n = cobs.encodeInto(src, dst);
  assert.deepEqual(cobs.decode(dst.subarray(0, n)), src);
});

test('createCodec isolates linear memory and memoryBytes is observable', () => {
  const codec = cobs.createCodec();
  assert.ok(codec.memoryBytes >= 65536);
  assert.deepEqual(codec.decode(codec.encode(u8(1, 0, 2))), u8(1, 0, 2));
});

test('a large payload then a small one: catches a view cached across grow', () => {
  // memory.grow() detaches the old buffer, so a hoisted view goes zero-length and
  // writes are dropped. The second call is the test.
  const codec = cobs.createCodec();
  const big = ramp(2 * 1024 * 1024, i => (i % 254) + 1);
  assert.deepEqual(codec.decode(codec.encode(big)), big);
  assert.ok(codec.memoryBytes > 262144, 'expected memory to have grown');
  assert.deepEqual(codec.decode(codec.encode(u8(1, 2, 3))), u8(1, 2, 3));
});



