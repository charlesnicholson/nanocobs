// SPDX-License-Identifier: Unlicense OR 0BSD
//
// nanocobs — Consistent Overhead Byte Stuffing. The nanocobs C implementation
// compiled to WebAssembly. Frames include their trailing 0x00 delimiter.

import { WASM_BASE64 } from './wasm.js';
import { fromBase64 } from './base64.js';
import {
  MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax, assertBytes, checkPayloadLen,
} from './shared.js';

export { MAX_PAYLOAD_BYTES, CobsError, encodeMax, decodeMax };

// Always present, so a caller can log which implementation it got without caring
// whether it resolved the Node entry or this one. ./node.js overrides it when a
// native prebuild loads.
export const backend = 'wasm';

const PAGE = 65536;
const EXPECTED_ABI = 1;

export const wasmModule = new WebAssembly.Module(fromBase64(WASM_BASE64));

export function createCodec(options = {}) {
  const mod = options.module ?? wasmModule;
  const ex = new WebAssembly.Instance(mod, {}).exports;   // no imports, by construction

  const abi = ex.cobs_wasm_abi_version();
  if (abi !== EXPECTED_ABI) {
    throw new Error(`nanocobs: wasm ABI ${abi}, expected ${EXPECTED_ABI}`);
  }

  const scratch = ex.cobs_wasm_heap_base();  // 8-byte out-param slot for consumed
  const base = scratch + 8;
  // The one cached view; reassigned only in reserve(). memory.grow() detaches the
  // old buffer and leaves every view over it zero-length.
  let mem = new Uint8Array(ex.memory.buffer);

  const reserve = (need) => {
    if (base + need <= mem.length) return;
    const pages = Math.ceil((base + need - mem.length) / PAGE);
    try {
      ex.memory.grow(pages);
    } catch (cause) {
      throw new RangeError(
        `nanocobs: cannot grow wasm memory to ${base + need} bytes`, { cause });
    }
    mem = new Uint8Array(ex.memory.buffer);
  };

  // Input and output back to back with an 8-byte gap, so they never overlap at
  // srcLen 0. The base is nonzero: cobs_encode rejects a null src even at len 0.
  const run = (fn, src, cap, wantConsumed) => {
    const inPtr = base;
    const outPtr = base + ((src.length + 7) & ~7) + 8;
    reserve(outPtr - base + cap);
    const m = mem;                      // read after reserve; never hoist this
    m.set(src, inPtr);
    const r = wantConsumed === undefined
      ? fn(inPtr, src.length, outPtr, cap)
      : fn(inPtr, src.length, outPtr, cap, scratch);
    return { r, outPtr, m };
  };

  // Valid only right after a successful decode that asked for it.
  const readConsumed = (m) =>
    m[scratch] | (m[scratch + 1] << 8) | (m[scratch + 2] << 16) | (m[scratch + 3] << 24);

  const check = (r, context) => {
    if (r < 0) throw new CobsError(-r, context);
    return r;
  };

  function encodeInto(payload, frame, throwOnError = true) {
    assertBytes(payload, 'payload');
    assertBytes(frame, 'frame');
    checkPayloadLen(payload.length);
    const cap = Math.min(frame.length, encodeMax(payload.length));
    const { r, outPtr, m } = run(ex.cobs_wasm_encode, payload, cap, 'encode');
    if (r < 0) return throwOnError ? check(r, 'encode') : r;
    frame.set(m.subarray(outPtr, outPtr + r));
    return r;
  }

  function decodeInto(frame, payload, throwOnError = true) {
    assertBytes(frame, 'frame');
    assertBytes(payload, 'payload');
    const cap = Math.min(payload.length, decodeMax(frame.length));
    const { r, outPtr, m } = run(ex.cobs_wasm_decode, frame, cap, 0);
    if (r < 0) return throwOnError ? check(r, 'decode') : r;
    payload.set(m.subarray(outPtr, outPtr + r));
    return r;
  }

  function encode(payload) {
    assertBytes(payload, 'payload');
    const cap = encodeMax(payload.length);
    const { r, outPtr, m } = run(ex.cobs_wasm_encode, payload, cap, 'encode');
    check(r, 'encode');
    return m.slice(outPtr, outPtr + r);      // the slice is the trim
  }

  // Stops at the first delimiter and ignores whatever follows, exactly as cobs_decode
  // does: a buffer may hold more than one frame. Use decodeFirst() to learn how many
  // bytes the frame occupied and walk to the next one.
  function decode(frame) {
    assertBytes(frame, 'frame');
    const cap = decodeMax(frame.length);
    const { r, outPtr, m } = run(ex.cobs_wasm_decode, frame, cap, 0);
    check(r, 'decode');
    return m.slice(outPtr, outPtr + r);
  }

  // Decode the frame at the front of a buffer that may hold more, and report the
  // bytes it occupied. The count is cobs_decode's out_enc_consumed, so the caller
  // scans for nothing to find the next frame.
  //
  // For a buffer of back-to-back frames, reach for decodeFrames(): each decodeFirst()
  // copies the whole buffer it is handed into wasm memory, so walking with
  // decodeFirst(buf.subarray(i)) recopies the tail once per frame and costs O(n^2).
  function decodeFirst(buf) {
    assertBytes(buf, 'buf');
    if (buf.length < 2) throw new CobsError(1, 'decodeFirst');
    const cap = decodeMax(buf.length);
    const { r, outPtr, m } = run(ex.cobs_wasm_decode, buf, cap, scratch);
    check(r, 'decodeFirst');
    return { payload: m.slice(outPtr, outPtr + r), consumed: readConsumed(m) };
  }

  // Every complete frame in |buf|, with one copy into wasm memory and the walk done
  // at advancing offsets inside it: linear in |buf| however many frames it holds.
  //
  // |consumed| is how much of |buf| held complete frames. Anything past it is a
  // partial frame -- the usual tail of a socket read -- for the caller to carry over
  // and prepend to the next chunk.
  //
  // An earlier version of this walked buf.indexOf(0) instead, because cobs_decode had
  // no way to say how much of the input a frame occupied. It does now, so the scan is
  // gone and the frame boundaries come from the decoder itself.
  function decodeFrames(buf) {
    assertBytes(buf, 'buf');
    const frames = [];
    if (buf.length < 2) return { frames, consumed: 0 };

    const inPtr = base;
    const outPtr = base + ((buf.length + 7) & ~7) + 8;
    reserve(outPtr - base + decodeMax(buf.length));
    const m = mem;                      // read after reserve; never hoist this
    m.set(buf, inPtr);

    let off = 0;
    while (buf.length - off >= 2) {
      const left = buf.length - off;
      // decodeMax(left) is the proven bound for the frame at this offset, so the
      // destination can never be the thing that is too small.
      const r = ex.cobs_wasm_decode(inPtr + off, left, outPtr, decodeMax(left), scratch);
      if (r < 0) {
        // Hence EXHAUSTED has one meaning here: the input ran out mid-frame. That is
        // a partial tail, not an error -- stop and let |consumed| report it.
        if (-r === 3) break;
        throw new CobsError(-r, 'decodeFrames');
      }
      frames.push(m.slice(outPtr, outPtr + r));
      const used = readConsumed(m);
      // cobs_decode's consumed is always >= 2 on success. Guarded anyway: a zero here
      // would spin forever, and this runs on whatever a socket delivered.
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
    get memoryBytes() { return mem.length; },
  };
}

const shared = createCodec();

export const encode = shared.encode;
export const decode = shared.decode;
export const decodeFirst = shared.decodeFirst;
export const decodeFrames = shared.decodeFrames;
export const encodeInto = shared.encodeInto;
export const decodeInto = shared.decodeInto;
export const tryEncodeInto = shared.tryEncodeInto;
export const tryDecodeInto = shared.tryDecodeInto;
