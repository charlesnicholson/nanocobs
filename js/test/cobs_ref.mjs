// SPDX-License-Identifier: Unlicense OR 0BSD
//
// A byte-loop COBS in JavaScript, mirroring cobs.c with no SWAR and no wasm. The JS
// counterpart of tests/cobs_ref.c, for the differential tests.
//
// The *Into forms are the primitives, so a caller can impose an output capacity and
// hit the same EXHAUSTED boundaries the C tests sweep.

export const RET = { SUCCESS: 0, BAD_ARG: 1, BAD_PAYLOAD: 2, EXHAUSTED: 3 };

export function refEncodeMax(n) {
  return 1 + n + Math.ceil(n / 254) + (n === 0 ? 1 : 0);
}

export function refDecodeMax(frameLen) {
  return Math.max(0, frameLen - 2);
}

export function refEncodeInto(src, dst) {
  if (dst.length < 2) return { ret: RET.BAD_ARG };      // cobs.c:214
  const n = src.length;
  let si = 0, di = 1, codeIdx = 0, code = 1;

  while (si < n) {
    for (;;) {
      if (di >= dst.length) return { ret: RET.EXHAUSTED };
      const b = src[si];
      if (b) { dst[di++] = b; ++code; }
      if (b === 0 || code === 0xFF) {
        dst[codeIdx] = code;
        codeIdx = di;
        code = 1;
        // Not advanced on the last source byte with code == 0xFF: the delimiter
        // overwrites the code byte there (cobs.c:275-279).
        if (b === 0 || si + 1 < n) ++di;
        ++si;
        break;
      }
      ++si;
      if (si >= n) break;
    }
  }

  if (di >= dst.length) return { ret: RET.EXHAUSTED };
  dst[codeIdx] = code;
  dst[di++] = 0;
  return { ret: RET.SUCCESS, len: di };
}

export function refDecodeInto(frame, out) {
  const m = frame.length;
  if (m < 2) return { ret: RET.BAD_ARG };               // cobs.c:509
  let si = 0, di = 0;

  while (si < m) {
    // cobs.c reads the code byte with no zero test: a 0x00 code makes the "block - 1"
    // below wrap, so it reads as an unbounded block and the bounds -- not a special
    // case -- decide between BAD_PAYLOAD and EXHAUSTED. Rejecting it here would model
    // a check cobs.c does not have, and would disagree whenever out is tight.
    const code = frame[si++];

    let block = code;
    while (((block - 1) >>> 0) !== 0) {                 // cobs.c: while (block - 1)
      // Both bounds before the read, per cobs.c:632-634. Checking the destination
      // after the zero test would say BAD_PAYLOAD where the C says EXHAUSTED.
      if (si >= m || di >= out.length) return { ret: RET.EXHAUSTED };
      block = (block - 1) >>> 0;
      const b = frame[si++];
      if (b === 0) return { ret: RET.BAD_PAYLOAD };     // interior zero inside a block
      out[di++] = b;
    }

    if (si >= m) return { ret: RET.EXHAUSTED };
    if (frame[si] === 0) return { ret: RET.SUCCESS, len: di };
    // A block that ran to the 254-byte limit encodes no zero.
    if (code !== 0xFF) {
      if (di >= out.length) return { ret: RET.EXHAUSTED };
      out[di++] = 0;
    }
  }
  return { ret: RET.EXHAUSTED };
}

/** Allocating wrappers, for when the bound is known to suffice. */
export function refEncode(src) {
  const dst = new Uint8Array(refEncodeMax(src.length));
  const r = refEncodeInto(src, dst);
  return r.ret === RET.SUCCESS
    ? { ret: r.ret, out: dst.subarray(0, r.len) }
    : { ret: r.ret };
}

export function refDecode(frame) {
  const out = new Uint8Array(refDecodeMax(frame.length));
  const r = refDecodeInto(frame, out);
  return r.ret === RET.SUCCESS
    ? { ret: r.ret, out: out.subarray(0, r.len) }
    : { ret: r.ret };
}
