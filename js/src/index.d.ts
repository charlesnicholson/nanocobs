// SPDX-License-Identifier: Unlicense OR 0BSD

export type CobsErrorCode = 'BAD_ARG' | 'BAD_PAYLOAD' | 'EXHAUSTED';

export class CobsError extends Error {
  readonly name: 'CobsError';
  readonly code: CobsErrorCode;
  /** The numeric cobs_ret_t: 1 BAD_ARG, 2 BAD_PAYLOAD, 3 EXHAUSTED. */
  readonly ret: 1 | 2 | 3;
  static isCobsError(e: unknown): e is CobsError;
}

/** Largest payload this package accepts: 512 MiB. A limit of the JS entry points, not
 *  of the library -- cobs.c is bounded only by size_t and handles far more. It is
 *  wasm32's ceiling, since a payload and its encoding have to share one linear memory,
 *  and the Node wrapper applies the same number so a program that works on one backend
 *  works on the other. Over it, `encode` and `encodeInto` throw a RangeError rather
 *  than a CobsError. */
export const MAX_PAYLOAD_BYTES: number;
export const wasmModule: WebAssembly.Module;

/** Which implementation loaded. 'native' only on Node, and only where the package
 *  carries a prebuild for this platform/arch/libc; 'wasm' everywhere else, including
 *  browsers, bundlers, Deno and Workers. The two are behaviourally identical, so this
 *  is a performance signal -- log it, because a silent fallback otherwise looks
 *  exactly like the native path. Set NANOCOBS_BACKEND=wasm to force the portable one,
 *  or =native to turn a missing prebuild into a startup error. */
export const backend: 'native' | 'wasm';

/** Largest frame `encode` can produce, delimiter included. Mirrors COBS_ENCODE_MAX. */
export function encodeMax(decodedLen: number): number;
/** Largest payload a `frameLen`-byte frame can decode to: frameLen - 2, min 0. */
export function decodeMax(frameLen: number): number;

export function encode(payload: Uint8Array): Uint8Array;
/** Decodes the frame at the front of `frame` and ignores anything after its
 *  delimiter, like cobs_decode. Use `decodeFirst` to also learn how many bytes the
 *  frame occupied, and so walk a buffer of back-to-back frames. */
export function decode(frame: Uint8Array): Uint8Array;
export function encodeInto(payload: Uint8Array, frame: Uint8Array): number;
export function decodeInto(frame: Uint8Array, payload: Uint8Array): number;
/** Non-throwing: >= 0 is bytes written, < 0 is the negated cobs_ret_t. */
export function tryEncodeInto(payload: Uint8Array, frame: Uint8Array): number;
export function tryDecodeInto(frame: Uint8Array, payload: Uint8Array): number;
/** Decode the frame at the front of `buf`, which may hold more after it.
 *  `consumed` is cobs_decode's `out_enc_consumed`, so finding the next frame needs
 *  no scan. To walk a whole buffer use `decodeFrames`: this copies all of `buf` into
 *  wasm memory per call, so a decodeFirst(buf.subarray(i)) loop is O(n^2). */
export function decodeFirst(buf: Uint8Array): { payload: Uint8Array; consumed: number };

/** Every complete frame in `buf`, copied in once and walked in place: linear in
 *  `buf` however many frames it holds. `consumed` is how much of `buf` held complete
 *  frames, so `buf.subarray(consumed)` is the partial tail to carry to the next read:
 *
 *      let carry = new Uint8Array(0);
 *      socket.on('data', (chunk) => {
 *        const buf = concat(carry, chunk);
 *        const { frames, consumed } = decodeFrames(buf);
 *        for (const f of frames) handle(f);
 *        carry = buf.subarray(consumed);
 *      });
 */
export function decodeFrames(buf: Uint8Array): { frames: Uint8Array[]; consumed: number };

export interface Codec {
  encode(payload: Uint8Array): Uint8Array;
  decode(frame: Uint8Array): Uint8Array;
  decodeFirst(buf: Uint8Array): { payload: Uint8Array; consumed: number };
  decodeFrames(buf: Uint8Array): { frames: Uint8Array[]; consumed: number };
  encodeInto(payload: Uint8Array, frame: Uint8Array): number;
  decodeInto(frame: Uint8Array, payload: Uint8Array): number;
  tryEncodeInto(payload: Uint8Array, frame: Uint8Array): number;
  tryDecodeInto(frame: Uint8Array, payload: Uint8Array): number;
  /** Bytes of wasm linear memory held. Grows on demand, never shrinks. */
  readonly memoryBytes: number;
}

/** A codec with its own linear memory, to reclaim what a large payload pinned or
 *  to isolate a worker. */
export function createCodec(options?: { module?: WebAssembly.Module }): Codec;
