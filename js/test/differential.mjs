// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Shipping wasm against the byte-loop JS reference, encode and decode, for one
// payload. Mirrors check() in test_swar_runs.cc and diff_all() in the differential.

import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { RET, refEncodeInto, refDecodeInto, refEncodeMax, refDecodeMax } from './cobs_ref.mjs';

const POISON = 0xCD;

const hex = (a, limit = 24) =>
  '[' + [...a.subarray(0, limit)].map(b => b.toString(16).padStart(2, '0')).join(' ')
      + (a.length > limit ? ` ...+${a.length - limit}` : '') + ']';

/** Encode into a poisoned buffer of exactly `cap`, so a lane that writes too much
 *  is visible. */
function encodeBoth(payload, cap) {
  const a = new Uint8Array(cap + 8).fill(POISON);
  const b = new Uint8Array(cap + 8).fill(POISON);
  const ra = refEncodeInto(payload, a.subarray(0, cap));
  const rb = cobs.tryEncodeInto(payload, b.subarray(0, cap));
  return {
    ref: { ret: ra.ret, len: ra.len ?? 0, buf: a },
    swar: { ret: rb < 0 ? -rb : RET.SUCCESS, len: rb < 0 ? 0 : rb, buf: b },
  };
}

// The package rejects a displaced delimiter before the C sees it. cobs_ref.mjs stays
// a faithful cobs.c model, so the package's extra rule lives here.
function refDecodePackage(frame, out) {
  const z = frame.indexOf(0);
  if (z !== frame.length - 1) return { ret: RET.BAD_PAYLOAD };
  return refDecodeInto(frame, out);
}

function decodeBoth(frame, cap) {
  const a = new Uint8Array(cap + 8).fill(POISON);
  const b = new Uint8Array(cap + 8).fill(POISON);
  const ra = refDecodePackage(frame, a.subarray(0, cap));
  const rb = cobs.tryDecodeInto(frame, b.subarray(0, cap));
  return {
    ref: { ret: ra.ret, len: ra.len ?? 0, buf: a },
    swar: { ret: rb < 0 ? -rb : RET.SUCCESS, len: rb < 0 ? 0 : rb, buf: b },
  };
}

function agree(what, ref, swar, extra = '') {
  assert.equal(swar.ret, ref.ret,
    `${what}: ret differs (ref=${ref.ret} swar=${swar.ret}) ${extra}`);
  if (ref.ret !== RET.SUCCESS) return;
  assert.equal(swar.len, ref.len,
    `${what}: len differs (ref=${ref.len} swar=${swar.len}) ${extra}`);
  assert.deepEqual(swar.buf.subarray(0, ref.len), ref.buf.subarray(0, ref.len),
    `${what}: bytes differ ${extra}\n  ref  ${hex(ref.buf.subarray(0, ref.len))}` +
    `\n  swar ${hex(swar.buf.subarray(0, ref.len))}`);
  // Nothing past the reported length may be touched.
  assert.ok(swar.buf.subarray(ref.len).every(v => v === POISON),
    `${what}: wrote past the reported length ${extra}`);
}

/** Encode differentially at a chosen capacity. */
export function diffEncode(payload, cap, what = 'encode') {
  const { ref, swar } = encodeBoth(payload, cap);
  agree(what, ref, swar, `len=${payload.length} cap=${cap}`);
  return ref;
}

/** Decode differentially at a chosen capacity. */
export function diffDecode(frame, cap, what = 'decode') {
  const { ref, swar } = decodeBoth(frame, cap);
  agree(what, ref, swar, `frame=${frame.length} cap=${cap}`);
  return ref;
}

/** Both directions at the natural capacities, plus a round-trip. */
export function diffAll(payload, what = '') {
  const n = payload.length;
  const enc = diffEncode(payload, refEncodeMax(n), `encode ${what}`);
  assert.equal(enc.ret, RET.SUCCESS, `encode ${what}: expected success at the full bound`);

  const frame = enc.buf.subarray(0, enc.len);
  const dec = diffDecode(frame, refDecodeMax(frame.length), `decode ${what}`);
  assert.equal(dec.ret, RET.SUCCESS, `decode ${what}: expected success`);
  assert.deepEqual(dec.buf.subarray(0, dec.len), payload,
    `round-trip ${what}: len=${n}\n  got ${hex(dec.buf.subarray(0, dec.len))}` +
    `\n  want ${hex(payload)}`);
}
