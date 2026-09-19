// SPDX-License-Identifier: Unlicense OR 0BSD
//
// decodeFrames() is the linear-time walk over a buffer of back-to-back frames: one copy
// into wasm memory, then decoding at advancing offsets inside it. decodeFirst() on
// successive subarrays recopies the tail per frame and is quadratic in frame count.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { u8, concat } from './helpers.mjs';

const framesOf = (payloads) => concat(...payloads.map((p) => cobs.encode(p)));

test('decodeFrames returns every frame and how much it consumed', () => {
  const payloads = [u8(1, 2, 3), u8(), u8(0, 0, 0), u8(...Array(600).fill(7))];
  const buf = framesOf(payloads);
  const got = cobs.decodeFrames(buf);
  assert.equal(got.consumed, buf.length);
  assert.equal(got.frames.length, payloads.length);
  for (let i = 0; i < payloads.length; ++i) {
    assert.deepEqual(got.frames[i], payloads[i], `frame ${i}`);
  }
});

test('decodeFrames agrees with a decodeFirst walk', () => {
  const payloads = Array.from({ length: 40 }, (_, i) =>
    u8(...Array.from({ length: (i * 7) % 61 }, (_, k) => (i + k) % 256)));
  const buf = framesOf(payloads);

  const walked = [];
  for (let i = 0; i < buf.length; ) {
    const { payload, consumed } = cobs.decodeFirst(buf.subarray(i));
    walked.push(payload);
    i += consumed;
  }
  assert.deepEqual(cobs.decodeFrames(buf).frames, walked);
});

test('decodeFrames stops at a partial trailing frame and reports the carry', () => {
  const whole = framesOf([u8(1, 2), u8(3, 4)]);
  const partial = cobs.encode(u8(5, 6, 7)).subarray(0, 3);   // cut before the delimiter
  const buf = concat(whole, partial);

  const { frames: got, consumed } = cobs.decodeFrames(buf);
  assert.deepEqual(got, [u8(1, 2), u8(3, 4)]);
  assert.equal(consumed, whole.length);
  // The carry, prepended to the rest, decodes on the next round.
  const rest = concat(buf.subarray(consumed), cobs.encode(u8(5, 6, 7)).subarray(3));
  assert.deepEqual(cobs.decodeFrames(rest).frames, [u8(5, 6, 7)]);
});

test('decodeFrames on a buffer too short to hold a frame consumes nothing', () => {
  for (const b of [u8(), u8(0x01)]) {
    assert.deepEqual(cobs.decodeFrames(b), { frames: [], consumed: 0 });
  }
});

test('decodeFrames throws on a malformed frame rather than skipping it', () => {
  const buf = concat(cobs.encode(u8(1, 2)), u8(0x04, 0x01, 0x00, 0x03, 0x00));
  assert.throws(() => cobs.decodeFrames(buf), (e) => e.code === 'BAD_PAYLOAD');
  for (const bad of [null, [1, 0], 'ab', new Int8Array(2)]) {
    assert.throws(() => cobs.decodeFrames(bad), TypeError);
  }
});

test('decodeFrames is linear in the buffer, not quadratic in the frame count', () => {
  const build = (k) => framesOf(Array.from({ length: k }, () => u8(...Array(64).fill(9))));
  const nsPerFrame = (k) => {
    const buf = build(k);
    for (let w = 0; w < 50; ++w) cobs.decodeFrames(buf);
    const reps = 50;
    const t0 = process.hrtime.bigint();
    for (let r = 0; r < reps; ++r) cobs.decodeFrames(buf);
    return Number(process.hrtime.bigint() - t0) / reps / k;
  };
  // Quadratic would grow this ~8x from 100 to 800 frames; linear leaves it flat. The
  // bar is deliberately loose -- this is a shape check, not a benchmark.
  const small = nsPerFrame(100), large = nsPerFrame(800);
  assert.ok(large < small * 3,
            `per-frame cost grew ${(large / small).toFixed(1)}x from 100 to 800 frames ` +
            `(${small.toFixed(0)} -> ${large.toFixed(0)} ns); decodeFrames should be linear`);
});
