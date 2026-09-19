// SPDX-License-Identifier: Unlicense OR 0BSD
//
// What both backends are built from. The wasm and native implementations differ only
// in where the bytes live; error objects, the bounds math and the argument checks are
// this file for both, so those can never drift apart.

// The largest payload either entry point accepts. This is a policy of the JS wrappers,
// not of cobs.c: the library takes a size_t and handles far more than this. The number
// is wasm32's ceiling -- a payload and its encoding must share one linear memory -- and
// the native path applies it too, so a program that works on one backend works on the
// other rather than failing only once it is deployed somewhere without a prebuild.
export const MAX_PAYLOAD_BYTES = 0x20000000;

const RET_NAMES = { 1: 'BAD_ARG', 2: 'BAD_PAYLOAD', 3: 'EXHAUSTED' };
const RET_DETAIL = {
  1: 'invalid argument',
  2: 'malformed COBS frame',
  // decode sizes output to the proven bound, so from it this only ever means the input
  // ran out: a code byte, or the missing delimiter, pointed past the end.
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

/** Largest frame `encode` can produce, delimiter included. Mirrors COBS_ENCODE_MAX. */
export function encodeMax(decodedLen) {
  return 1 + decodedLen + Math.ceil(decodedLen / 254) + (decodedLen === 0 ? 1 : 0);
}

// A proven bound: every frame spends at least one code byte and one delimiter.
export function decodeMax(frameLen) {
  return Math.max(0, frameLen - 2);
}

// instanceof is false for a typed array from another realm, and Buffer must pass.
export function assertBytes(x, name) {
  if (Object.prototype.toString.call(x) !== '[object Uint8Array]') {
    throw new TypeError(`nanocobs: ${name} must be a Uint8Array, got ${typeof x}`);
  }
}

export function checkPayloadLen(n) {
  if (n > MAX_PAYLOAD_BYTES) {
    throw new RangeError(
      `nanocobs: payload of ${n} bytes exceeds MAX_PAYLOAD_BYTES (${MAX_PAYLOAD_BYTES})`);
  }
}
