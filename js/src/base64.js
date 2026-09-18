// SPDX-License-Identifier: Unlicense OR 0BSD

// Decode the inlined wasm with no dependency. Fastest first; the last branch is so
// the package does not assume Node.
export function fromBase64(s) {
  if (typeof Uint8Array.fromBase64 === 'function') {
    return Uint8Array.fromBase64(s);                       // ES2025
  }
  if (typeof Buffer !== 'undefined') {
    const b = Buffer.from(s, 'base64');
    return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
  }
  const bin = atob(s);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; ++i) out[i] = bin.charCodeAt(i);
  return out;
}
