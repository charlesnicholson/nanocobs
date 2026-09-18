// SPDX-License-Identifier: Unlicense OR 0BSD

export type CobsErrorCode = 'BAD_ARG' | 'BAD_PAYLOAD' | 'EXHAUSTED';

export class CobsError extends Error {
  readonly name: 'CobsError';
  readonly code: CobsErrorCode;
  /** The numeric cobs_ret_t: 1 BAD_ARG, 2 BAD_PAYLOAD, 3 EXHAUSTED. */
  readonly ret: 1 | 2 | 3;
  static isCobsError(e: unknown): e is CobsError;
}

export const MAX_PAYLOAD_BYTES: number;
export const wasmModule: WebAssembly.Module;

/** Largest frame `encode` can produce, delimiter included. Mirrors COBS_ENCODE_MAX. */
export function encodeMax(decodedLen: number): number;
/** Largest payload a `frameLen`-byte frame can decode to: frameLen - 2, min 0. */
export function decodeMax(frameLen: number): number;

export function encode(payload: Uint8Array): Uint8Array;
/** `frame` must end with 0x00 and hold no other 0x00; see `decodeFirst` for a
 *  buffer of back-to-back frames. */
export function decode(frame: Uint8Array): Uint8Array;
export function encodeInto(payload: Uint8Array, frame: Uint8Array): number;
export function decodeInto(frame: Uint8Array, payload: Uint8Array): number;
/** Non-throwing: >= 0 is bytes written, < 0 is the negated cobs_ret_t. */
export function tryEncodeInto(payload: Uint8Array, frame: Uint8Array): number;
export function tryDecodeInto(frame: Uint8Array, payload: Uint8Array): number;
/** Decode the frame at the front of `buf`, which may hold more after it.
 *  `consumed` is cobs_decode's `out_enc_consumed`, so a walker scans for nothing:
 *
 *      let i = 0;
 *      while (i < buf.length) {
 *        const { payload, consumed } = decodeFirst(buf.subarray(i));
 *        handle(payload);
 *        i += consumed;
 *      }
 */
export function decodeFirst(buf: Uint8Array): { payload: Uint8Array; consumed: number };

export interface Codec {
  encode(payload: Uint8Array): Uint8Array;
  decode(frame: Uint8Array): Uint8Array;
  decodeFirst(buf: Uint8Array): { payload: Uint8Array; consumed: number };
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
