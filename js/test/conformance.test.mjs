// SPDX-License-Identifier: Unlicense OR 0BSD
//
// The two backends must be indistinguishable. Everything here runs the same input
// through wasm and through the native addon and requires the same answer -- the same
// bytes on success, the same error code on failure, the same number from the try*
// forms. Nothing asserts a specific value; the point is that neither backend can drift
// from the other, whatever the C does.
//
// Skipped with a visible message when this host has no prebuild, so a wasm-only
// machine still runs the rest of the suite.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import * as wasm from '../src/index.js';
import { createNativeCodec } from '../src/native.js';
import { loadNative } from '../src/loader.js';
import { u8, concat } from './helpers.mjs';
import { SHAPES, makeShape } from './shapes.mjs';

const addon = loadNative();
const native = addon ? createNativeCodec(addon) : null;

// Outcome, not value: a Uint8Array, a number, or the name of what was thrown. Both
// backends produce one of these and they have to match.
function outcome(fn) {
  try {
    const v = fn();
    return v instanceof Uint8Array ? { bytes: [...v] }
         : typeof v === 'object' && v !== null ? JSON.parse(JSON.stringify(v, (k, x) =>
             ArrayBuffer.isView(x) ? [...x] : x))
         : { value: v };
  } catch (e) {
    return { threw: e.name, code: e.code ?? null };
  }
}

function agree(what, run) {
  assert.deepEqual(outcome(() => run(native)), outcome(() => run(wasm)), what);
}

// Payloads that exercise the shapes the C tests care about, at the sizes where block
// boundaries and the SWAR lane change behaviour.
const SIZES = [0, 1, 2, 3, 253, 254, 255, 256, 508, 509, 510, 1024, 4096];
const CORPUS = [];
for (const shape of SHAPES) {
  for (const n of SIZES) CORPUS.push({ shape, n, payload: makeShape(shape, n, 0xB0B0) });
}

// Frames that are wrong in every way the decoder distinguishes, including the two
// leading-0x00 cases whose answer depends on which bound is hit first.
const MALFORMED = [
  [], [0x00], [0x01], [0x09, 0x09], [0x02, 0x01], [0x03, 0x00],
  [0x05, 0x01, 0x00, 0x00, 0x01, 0x00], [0x04, 0x01, 0x00, 0x03, 0x00],
  [0x00, 0x09, 0x00, 0x00], [0x00, 0x00], [0x01, 0x00, 0x01, 0x00],
  [0xff, 0x01, 0x00], [0x02, 0x00, 0x00],
];

test('backends agree on encode and decode', { skip: native ? false : 'no prebuild for this host' }, () => {
  for (const { shape, n, payload } of CORPUS) {
    agree(`encode ${shape}/${n}`, (c) => c.encode(payload));
    const frame = wasm.encode(payload);
    agree(`decode ${shape}/${n}`, (c) => c.decode(frame));
    agree(`decodeFirst ${shape}/${n}`, (c) => c.decodeFirst(frame));
  }
});

test('backends agree on the into forms at every capacity', { skip: native ? false : 'no prebuild' }, () => {
  for (const { shape, n, payload } of CORPUS.filter((c) => c.n <= 510)) {
    const need = wasm.encodeMax(n);
    // Exactly right, one short, way short, and roomier than needed.
    for (const cap of [need, need - 1, 1, 0, need + 16]) {
      agree(`encodeInto ${shape}/${n} cap=${cap}`,
            (c) => c.encodeInto(payload, new Uint8Array(Math.max(0, cap))));
      agree(`tryEncodeInto ${shape}/${n} cap=${cap}`,
            (c) => c.tryEncodeInto(payload, new Uint8Array(Math.max(0, cap))));
    }
    const frame = wasm.encode(payload);
    for (const cap of [n, n - 1, 1, 0, n + 16]) {
      agree(`decodeInto ${shape}/${n} cap=${cap}`,
            (c) => c.decodeInto(frame, new Uint8Array(Math.max(0, cap))));
      agree(`tryDecodeInto ${shape}/${n} cap=${cap}`,
            (c) => c.tryDecodeInto(frame, new Uint8Array(Math.max(0, cap))));
    }
  }
});

test('backends agree on malformed input, error code included', { skip: native ? false : 'no prebuild' }, () => {
  for (const bytes of MALFORMED) {
    const frame = u8(...bytes);
    agree(`decode [${bytes}]`, (c) => c.decode(frame));
    agree(`decodeFirst [${bytes}]`, (c) => c.decodeFirst(frame));
    agree(`decodeFrames [${bytes}]`, (c) => c.decodeFrames(frame));
    // Capacity decides between BAD_PAYLOAD and EXHAUSTED, so vary it.
    for (const cap of [0, 1, 2, 8, 64]) {
      agree(`tryDecodeInto [${bytes}] cap=${cap}`,
            (c) => c.tryDecodeInto(frame, new Uint8Array(cap)));
    }
  }
});

test('backends agree on wrong argument types', { skip: native ? false : 'no prebuild' }, () => {
  for (const bad of [null, undefined, [1, 0], 'ab', new Int8Array(2), 7, {}]) {
    agree('encode', (c) => c.encode(bad));
    agree('decode', (c) => c.decode(bad));
    agree('decodeFrames', (c) => c.decodeFrames(bad));
    agree('encodeInto src', (c) => c.encodeInto(bad, new Uint8Array(8)));
    agree('encodeInto dst', (c) => c.encodeInto(u8(1), bad));
    agree('decodeInto src', (c) => c.decodeInto(bad, new Uint8Array(8)));
    agree('decodeInto dst', (c) => c.decodeInto(u8(1, 0), bad));
  }
});

test('backends agree walking multi-frame buffers', { skip: native ? false : 'no prebuild' }, () => {
  const payloads = [u8(1, 2, 3), u8(), u8(0, 0), makeShape('z_5pct', 700, 1), u8(0xff)];
  const buf = concat(...payloads.map((p) => wasm.encode(p)));
  agree('decodeFrames whole', (c) => c.decodeFrames(buf));

  // Every truncation point: each one is a different partial-tail carry.
  for (let cut = 0; cut <= buf.length; ++cut) {
    agree(`decodeFrames cut=${cut}`, (c) => c.decodeFrames(buf.subarray(0, cut)));
  }
  // And the walk itself, frame by frame.
  for (let i = 0; i < buf.length; ) {
    agree(`decodeFirst at ${i}`, (c) => c.decodeFirst(buf.subarray(i)));
    i += wasm.decodeFirst(buf.subarray(i)).consumed;
  }
});

// The vectors are cobs.c's own bytes, so agreeing here means agreeing with the C and
// not merely with each other.
test('backends agree on the golden vectors', { skip: native ? false : 'no prebuild' }, () => {
  const vectors = JSON.parse(
    readFileSync(new URL('./vectors.json', import.meta.url), 'utf8'));
  const b64 = (x) => new Uint8Array(Buffer.from(x, 'base64'));

  for (const v of vectors.encode) {
    const payload = b64(v.payload), frame = b64(v.frame);
    agree(`encode ${v.shape}/${v.len}`, (c) => c.encode(payload));
    agree(`decode ${v.shape}/${v.len}`, (c) => c.decode(frame));
    agree(`decodeFirst ${v.shape}/${v.len}`, (c) => c.decodeFirst(frame));
    // Both must also match the C's own recorded answer, not just each other.
    assert.deepEqual(native.encode(payload), frame, `C parity: encode ${v.shape}/${v.len}`);
    assert.equal(native.decodeFirst(frame).consumed, v.consumed,
                 `C parity: consumed ${v.shape}/${v.len}`);
  }

  for (const v of vectors.decode_errors) {
    const frame = b64(v.frame);
    agree(`decode error [${[...frame]}]`, (c) => c.decode(frame));
    agree(`decodeFrames error [${[...frame]}]`, (c) => c.decodeFrames(frame));
    for (const cap of [0, 1, 8, 64]) {
      agree(`tryDecodeInto error [${[...frame]}] cap=${cap}`,
            (c) => c.tryDecodeInto(frame, new Uint8Array(cap)));
    }
  }
});
