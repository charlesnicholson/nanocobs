// SPDX-License-Identifier: Unlicense OR 0BSD
//
// The native backend: the same public surface as ./index.js, over the N-API addon.
//
// Every capacity here is clamped exactly the way the wasm side clamps it, even where
// native could pass a roomier buffer. cobs_decode picks between BAD_PAYLOAD and
// EXHAUSTED by whichever bound it reaches first, so an unclamped destination would
// make the two backends disagree on malformed input. Parity is worth more than the
// handful of cases a bigger buffer would let through.

import {
  MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax, assertBytes, checkPayloadLen,
} from './shared.js';

export { MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax };

export function createNativeCodec(addon) {
  // One slot, reused: cobs_decode's out_enc_consumed is read before any call can
  // overwrite it, and the addon takes it as a parameter so it stays reentrant.
  const consumed = new Uint32Array(1);

  const check = (r, context) => {
    if (r < 0) throw new CobsError(-r, context);
    return r;
  };

  // The allocating entry points return a Uint8Array or a negative cobs_ret_t, so one
  // return value covers both and the wrapper decides whether to throw.
  const unwrap = (r, context) => {
    if (typeof r === 'number') throw new CobsError(-r, context);
    return r;
  };

  function encodeInto(payload, frame, throwOnError = true) {
    assertBytes(payload, 'payload');
    assertBytes(frame, 'frame');
    checkPayloadLen(payload.length);
    const cap = Math.min(frame.length, encodeMax(payload.length));
    const r = addon.encodeInto(payload, frame.subarray(0, cap));
    if (r < 0) return throwOnError ? check(r, 'encode') : r;
    return r;
  }

  function decodeInto(frame, payload, throwOnError = true) {
    assertBytes(frame, 'frame');
    assertBytes(payload, 'payload');
    const cap = Math.min(payload.length, decodeMax(frame.length));
    const r = addon.decodeInto(frame, payload.subarray(0, cap), null);
    if (r < 0) return throwOnError ? check(r, 'decode') : r;
    return r;
  }

  function encode(payload) {
    assertBytes(payload, 'payload');
    checkPayloadLen(payload.length);
    return unwrap(addon.encode(payload), 'encode');
  }

  // Stops at the first delimiter and ignores whatever follows, exactly as cobs_decode
  // does: a buffer may hold more than one frame.
  function decode(frame) {
    assertBytes(frame, 'frame');
    return unwrap(addon.decode(frame, null), 'decode');
  }

  function decodeFirst(buf) {
    assertBytes(buf, 'buf');
    if (buf.length < 2) throw new CobsError(1, 'decodeFirst');
    const payload = unwrap(addon.decode(buf, consumed), 'decodeFirst');
    return { payload, consumed: consumed[0] };
  }

  // Every complete frame in |buf|. One scratch buffer, sliced per frame, so the shape
  // of the work matches the wasm side and so does the per-frame capacity.
  function decodeFrames(buf) {
    assertBytes(buf, 'buf');
    const frames = [];
    if (buf.length < 2) return { frames, consumed: 0 };
    const scratch = new Uint8Array(decodeMax(buf.length));
    let off = 0;
    while (buf.length - off >= 2) {
      const left = buf.length - off;
      const r = addon.decodeInto(buf.subarray(off),
                                 scratch.subarray(0, decodeMax(left)),
                                 consumed);
      if (r < 0) {
        // Output is sized to the proven bound, so EXHAUSTED means the input ran out:
        // a partial frame at the tail, which is the caller's to carry.
        if (-r === 3) break;
        throw new CobsError(-r, 'decodeFrames');
      }
      frames.push(scratch.slice(0, r));
      const used = consumed[0];
      if (used <= 0) break;
      off += used;
    }
    return { frames, consumed: off };
  }

  return {
    encode, decode, decodeFirst, decodeFrames,
    encodeInto: (p, f) => encodeInto(p, f, true),
    decodeInto: (f, p) => decodeInto(f, p, true),
    tryEncodeInto: (p, f) => encodeInto(p, f, false),
    tryDecodeInto: (f, p) => decodeInto(f, p, false),
    // No linear memory to report: the codec works in the caller's buffers.
    get memoryBytes() { return 0; },
  };
}
