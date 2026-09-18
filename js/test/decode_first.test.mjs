// SPDX-License-Identifier: Unlicense OR 0BSD
//
// decodeFirst() surfaces cobs_decode's out_enc_consumed, so a caller can walk
// back-to-back frames without scanning. C counterpart: test_cobs_decode_consumed.cc.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { u8, runOf, concat } from './helpers.mjs';
import { SHAPES, makeShape } from './shapes.mjs';

const LENS = [0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 253, 254, 255, 256, 257, 507, 508,
              509, 510, 600, 1024];

test('a lone frame consumes exactly its own length, every shape and length', () => {
  for (const shape of SHAPES) {
    for (const n of LENS) {
      const payload = makeShape(shape, n, n + 1);
      const frame = cobs.encode(payload);
      const got = cobs.decodeFirst(frame);
      assert.equal(got.consumed, frame.length, `${shape} n=${n}`);
      assert.deepEqual(got.payload, payload, `${shape} n=${n}`);
    }
  }
});

test('trailing bytes after the frame are not consumed', () => {
  for (const n of [0, 1, 9, 254, 255, 256]) {
    const frame = cobs.encode(runOf(n, 0x41));
    for (let extra = 1; extra <= 8; ++extra) {
      for (const filler of [0x00, 0x01, 0xFF]) {
        const buf = concat(frame, runOf(extra, filler));
        const got = cobs.decodeFirst(buf);
        assert.equal(got.consumed, frame.length, `n=${n} extra=${extra} fill=${filler}`);
        assert.deepEqual(got.payload, runOf(n, 0x41));
      }
    }
  }
});

test('walking a multi-frame buffer lands exactly on the end', () => {
  for (const shape of SHAPES) {
    const payloads = LENS.map((n) => makeShape(shape, n, n + 3));
    const stream = concat(...payloads.map((p) => cobs.encode(p)));

    // The documented walker, verbatim.
    const got = [];
    let i = 0;
    while (i < stream.length) {
      const { payload, consumed } = cobs.decodeFirst(stream.subarray(i));
      got.push(payload);
      i += consumed;
    }
    assert.equal(i, stream.length, `${shape}: overshot or undershot`);
    assert.deepEqual(got, payloads, shape);
  }
});

test('walking works at every 0xFF-block boundary and word phase', () => {
  for (let lead = 0; lead <= 9; ++lead) {
    for (const run of [252, 253, 254, 255, 256, 507, 508, 509, 510, 762, 763]) {
      const payload = concat(runOf(lead, 0x00), runOf(run, 0x41), runOf(lead, 0x00));
      const frame = cobs.encode(payload);
      const buf = concat(frame, cobs.encode(u8(1, 2)));
      const first = cobs.decodeFirst(buf);
      assert.equal(first.consumed, frame.length, `lead=${lead} run=${run}`);
      assert.deepEqual(first.payload, payload, `lead=${lead} run=${run}`);
      assert.deepEqual(cobs.decodeFirst(buf.subarray(first.consumed)).payload, u8(1, 2));
    }
  }
});

test('consumed matches what the C recorded in the golden vectors', async () => {
  // vectors.json carries the C's own out_enc_consumed, so this compares against it.
  const { readFileSync } = await import('node:fs');
  const vectors = JSON.parse(
    readFileSync(new URL('./vectors.json', import.meta.url), 'utf8'));
  let checked = 0;
  for (const v of vectors.encode) {
    const frame = new Uint8Array(Buffer.from(v.frame, 'base64'));
    assert.equal(cobs.decodeFirst(frame).consumed, v.consumed, `${v.shape}/${v.len}`);
    ++checked;
  }
  assert.ok(checked > 400, `only ${checked} vectors carried a consumed count`);
});

test('decodeFirst rejects what the C rejects', () => {
  assert.throws(() => cobs.decodeFirst(u8()), (e) => e.code === 'BAD_ARG');
  assert.throws(() => cobs.decodeFirst(u8(0x01)), (e) => e.code === 'BAD_ARG');
  // decodeFirst sizes output at the proven bound, 0 for a 2-byte buffer, so the
  // destination check fires first. A roomier buffer says BAD_PAYLOAD; see
  // test_cobs_decode_consumed.cc.
  assert.throws(() => cobs.decodeFirst(u8(0x03, 0x00)), (e) => e.code === 'EXHAUSTED');
  // No terminating delimiter at all: the frame is truncated.
  assert.throws(() => cobs.decodeFirst(u8(0x02, 0x01)), (e) => e.code === 'EXHAUSTED');
  assert.throws(() => cobs.decodeFirst(u8(0x04, 0x01, 0x00, 0x03, 0x00)),
                (e) => e.code === 'BAD_PAYLOAD');
  for (const bad of [null, [1, 0], 'ab', new Int8Array(2)]) {
    assert.throws(() => cobs.decodeFirst(bad), TypeError);
  }
});

test('a partial trailing frame is reported as EXHAUSTED, so a stream can wait', () => {
  const frame = cobs.encode(runOf(40, 0x41));
  for (let cut = 2; cut < frame.length; ++cut) {
    assert.throws(() => cobs.decodeFirst(frame.subarray(0, cut)),
      (e) => cobs.CobsError.isCobsError(e), `cut at ${cut}`);
  }
  // The whole frame, however, decodes.
  assert.equal(cobs.decodeFirst(frame).consumed, frame.length);
});

test('decodeFirst accepts a pooled Buffer and a subarray view', () => {
  const payload = u8(7, 8, 0, 9);
  const frame = cobs.encode(payload);
  const slab = Buffer.allocUnsafe(256);
  const view = slab.subarray(64, 64 + frame.length);
  view.set(frame);
  assert.ok(view.byteOffset > 0);
  const got = cobs.decodeFirst(view);
  assert.equal(got.consumed, frame.length);
  assert.deepEqual(got.payload, payload);
});

test('decodeFirst does not disturb a following encode or decode', () => {
  // The out-param shares the arena, so a stale slot would show up as corruption.
  const a = cobs.encode(runOf(300, 0x41));
  for (let i = 0; i < 50; ++i) {
    const first = cobs.decodeFirst(concat(a, a));
    assert.equal(first.consumed, a.length);
    assert.deepEqual(cobs.encode(u8(1, 0, 2)), u8(0x02, 0x01, 0x02, 0x02, 0x00));
    assert.deepEqual(cobs.decode(cobs.encode(runOf(i, 0x7F))), runOf(i, 0x7F));
  }
});
