// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Shared helpers for the ported suites, standing in for byte_vec_t and the doctest
// idioms that carry meaning.

import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';

export const u8 = (...b) => new Uint8Array(b);

/** byte_vec_t(n, value) */
export const runOf = (n, value) => new Uint8Array(n).fill(value);

/** std::iota(v.begin(), v.end(), start) */
export const iota = (n, start = 0) =>
  Uint8Array.from({ length: n }, (_, i) => (start + i) & 0xFF);

export const concat = (...parts) => {
  const total = parts.reduce((s, p) => s + p.length, 0);
  const out = new Uint8Array(total);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
};

/** verify_frame_invariants() from test_cobs_encode.cc:21 */
export function verifyFrameInvariants(frame, what = '') {
  assert.ok(frame.length >= 2, `${what}: frame shorter than 2 bytes`);
  assert.equal(frame[frame.length - 1], 0x00, `${what}: no trailing delimiter`);
  assert.equal(frame.indexOf(0), frame.length - 1, `${what}: interior zero byte`);
}

/** The C tests' encode()/decode() helpers, which REQUIRE success. */
export function encodeOk(payload, what = '') {
  const frame = cobs.encode(payload);
  verifyFrameInvariants(frame, what);
  return frame;
}

export function roundTrip(payload, what = '') {
  const frame = encodeOk(payload, what);
  assert.deepEqual(cobs.decode(frame), payload, `${what}: round-trip mismatch`);
  return frame;
}
