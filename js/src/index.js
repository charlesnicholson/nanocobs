// SPDX-License-Identifier: Unlicense OR 0BSD
//
// nanocobs — Consistent Overhead Byte Stuffing. The nanocobs C implementation
// compiled to WebAssembly. Frames include their trailing 0x00 delimiter.

import { WASM_BASE64 } from './wasm.js';
import { fromBase64 } from './base64.js';

const PAGE = 65536;
const EXPECTED_ABI = 1;

// A payload and its encoding must both fit in one wasm32 linear memory.
export const MAX_PAYLOAD_BYTES = 0x20000000;

const RET_NAMES = { 1: 'BAD_ARG', 2: 'BAD_PAYLOAD', 3: 'EXHAUSTED' };
const RET_DETAIL = {
  1: 'invalid argument',
  2: 'malformed COBS frame',
  // decode always sizes output to the proven bound, so EXHAUSTED has one meaning.
  3: 'truncated frame: a code byte pointed past the end of the input',
};

export class CobsError extends Error {
  constructor(ret, context) {
    super(`nanocobs: ${context}: ${RET_DETAIL[ret] ?? 'unknown error'}`);
    this.name = 'CobsError';
    this.code = RET_NAMES[ret];
    this.ret = ret;
  }
  // Survives cross-realm and duplicate-instance boundaries; instanceof does not.
  static isCobsError(e) {
    return !!e && typeof e === 'object' && e.name === 'CobsError' && 'ret' in e;
  }
}

export function encodeMax(decodedLen) {
  return 1 + decodedLen + Math.ceil(decodedLen / 254) + (decodedLen === 0 ? 1 : 0);
}

// A proven bound: every frame spends at least one code byte and one delimiter.
export function decodeMax(frameLen) {
  return Math.max(0, frameLen - 2);
}

// instanceof is false for a typed array from another realm, and Buffer must pass.
function assertBytes(x, name) {
  if (Object.prototype.toString.call(x) !== '[object Uint8Array]') {
    throw new TypeError(`nanocobs: ${name} must be a Uint8Array, got ${typeof x}`);
  }
}

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
    if (payload.length > MAX_PAYLOAD_BYTES) {
      throw new RangeError(`nanocobs: payload of ${payload.length} bytes exceeds ` +
                           `MAX_PAYLOAD_BYTES (${MAX_PAYLOAD_BYTES})`);
    }
    const cap = Math.min(frame.length, encodeMax(payload.length));
    const { r, outPtr, m } = run(ex.cobs_wasm_encode, payload, cap, 'encode');
    if (r < 0) return throwOnError ? check(r, 'encode') : r;
    frame.set(m.subarray(outPtr, outPtr + r));
    return r;
  }

  function decodeInto(frame, payload, throwOnError = true) {
    assertBytes(frame, 'frame');
    assertBytes(payload, 'payload');
    // cobs_decode stops at the first delimiter and reports success, dropping the
    // rest. Wrong for a caller holding a socket read; decodeFirst() is for that.
    const z = frame.indexOf(0);
    if (z !== frame.length - 1) {
      if (!throwOnError) return -2;
      throw new CobsError(2, z < 0 ? 'decode (no trailing delimiter)'
                                   : 'decode (interior delimiter; use decodeFirst() to walk frames)');
    }
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

  function decode(frame) {
    assertBytes(frame, 'frame');
    const z = frame.indexOf(0);
    if (z !== frame.length - 1) {
      throw new CobsError(2, z < 0 ? 'decode (no trailing delimiter)'
                                   : 'decode (interior delimiter; use decodeFirst() to walk frames)');
    }
    const cap = decodeMax(frame.length);
    const { r, outPtr, m } = run(ex.cobs_wasm_decode, frame, cap, 0);
    check(r, 'decode');
    return m.slice(outPtr, outPtr + r);
  }

  // Decode the frame at the front of a buffer that may hold more, and report the
  // bytes it occupied. The count is cobs_decode's out_enc_consumed, so a walker
  // built on this scans for nothing:
  //
  //     let i = 0;
  //     while (i < buf.length) {
  //       const { payload, consumed } = decodeFirst(buf.subarray(i));
  //       handle(payload);
  //       i += consumed;
  //     }
  function decodeFirst(buf) {
    assertBytes(buf, 'buf');
    if (buf.length < 2) throw new CobsError(1, 'decodeFirst');
    const cap = decodeMax(buf.length);
    const { r, outPtr, m } = run(ex.cobs_wasm_decode, buf, cap, scratch);
    check(r, 'decodeFirst');
    return { payload: m.slice(outPtr, outPtr + r), consumed: readConsumed(m) };
  }

  return {
    encode, decode, decodeFirst,
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
export const encodeInto = shared.encodeInto;
export const decodeInto = shared.decodeInto;
export const tryEncodeInto = shared.tryEncodeInto;
export const tryDecodeInto = shared.tryDecodeInto;
