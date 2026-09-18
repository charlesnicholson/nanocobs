// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Ported from tests/test_swar_alignment.cc. COBS shifts dst one byte per block, so
// alignment drifts and there is no alignment-matched fast path. These prove there is
// no penalty and, more importantly, no wrong answer at some offset.
//
// The C sweeps pointer offsets; the JS analogue is a subarray view at every
// byteOffset. The in-place cases are not portable: the package never aliases.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { runOf, concat, u8 } from './helpers.mjs';
import { makeShape } from './shapes.mjs';

const LENS = [0, 1, 7, 8, 9, 15, 16, 17, 63, 64, 65, 253, 254, 255, 256, 257, 1023, 1024];

test('encode and decode at every src/dst offset pair', () => {
  for (const len of LENS) {
    const payload = makeShape('z_every_9', len, len + 1);
    const expected = cobs.encode(payload);

    for (let srcOfs = 0; srcOfs <= 8; ++srcOfs) {
      // Poisoned around the view, so a read outside it gives a wrong answer.
      const srcBacking = new Uint8Array(srcOfs + len + 8).fill(0xCD);
      srcBacking.set(payload, srcOfs);
      const src = srcBacking.subarray(srcOfs, srcOfs + len);

      assert.deepEqual(cobs.encode(src), expected, `len=${len} srcOfs=${srcOfs}`);

      for (let dstOfs = 0; dstOfs <= 8; ++dstOfs) {
        const dstBacking = new Uint8Array(dstOfs + cobs.encodeMax(len) + 8).fill(0xCD);
        const dst = dstBacking.subarray(dstOfs, dstOfs + cobs.encodeMax(len));
        const n = cobs.encodeInto(src, dst);

        assert.equal(n, expected.length, `len=${len} ${srcOfs}/${dstOfs}: length`);
        assert.deepEqual(dst.subarray(0, n), expected,
          `len=${len} ${srcOfs}/${dstOfs}: bytes`);
        // Nothing before or after the view may have moved.
        assert.ok(dstBacking.subarray(0, dstOfs).every(v => v === 0xCD),
          `len=${len} ${srcOfs}/${dstOfs}: wrote before the view`);
        assert.ok(dstBacking.subarray(dstOfs + n).every(v => v === 0xCD),
          `len=${len} ${srcOfs}/${dstOfs}: wrote past the reported length`);

        // And back again, through views at the same offsets.
        const frameBacking = new Uint8Array(srcOfs + n + 8).fill(0xCD);
        frameBacking.set(dst.subarray(0, n), srcOfs);
        const frame = frameBacking.subarray(srcOfs, srcOfs + n);

        const outBacking = new Uint8Array(dstOfs + cobs.decodeMax(n) + 8).fill(0xCD);
        const out = outBacking.subarray(dstOfs, dstOfs + cobs.decodeMax(n));
        const m = cobs.decodeInto(frame, out);
        assert.equal(m, len, `len=${len} ${srcOfs}/${dstOfs}: decoded length`);
        assert.deepEqual(out.subarray(0, m), payload,
          `len=${len} ${srcOfs}/${dstOfs}: decoded bytes`);
        assert.ok(outBacking.subarray(dstOfs + m).every(v => v === 0xCD),
          `len=${len} ${srcOfs}/${dstOfs}: decode wrote past its length`);
      }
    }
  }
});

test('long runs reach the fast lane at every offset', () => {
  // The paired C case decodes in place, which the package cannot. The intent holds:
  // runs long enough to engage a word lane, at every starting offset.
  for (const run of [253, 254, 255, 256, 508, 509]) {
    for (let ofs = 0; ofs <= 8; ++ofs) {
      const payload = concat(runOf(ofs, 0x00), runOf(run, 0x41), runOf(ofs, 0x00));
      const backing = new Uint8Array(ofs + payload.length + 8).fill(0xCD);
      backing.set(payload, ofs);
      const view = backing.subarray(ofs, ofs + payload.length);
      assert.deepEqual(cobs.decode(cobs.encode(view)), payload, `run=${run} ofs=${ofs}`);
    }
  }
});

test('a pooled Buffer arrives at an arbitrary byteOffset and still round-trips', () => {
  // Where a nonzero byteOffset comes from: Node pools small Buffers in a slab.
  for (const len of [1, 8, 9, 254, 255, 1024]) {
    const view = Buffer.allocUnsafe(len);
    for (let i = 0; i < len; ++i) view[i] = (i % 9 === 8) ? 0 : 1 + (i % 255);
    assert.deepEqual(cobs.decode(cobs.encode(view)), new Uint8Array(view),
      `len=${len} byteOffset=${view.byteOffset}`);
  }
});
