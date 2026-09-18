// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Assertions about the shipped wasm. wasm_inspect.py checks the binary at build
// time; this rechecks what the package depends on, through the bytes that ship.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as cobs from '../src/index.js';
import { WASM_BASE64, WASM_BYTE_LENGTH, WASM_ABI_VERSION } from '../src/wasm.js';
import { fromBase64 } from '../src/base64.js';

const bytes = fromBase64(WASM_BASE64);

test('the inlined base64 decodes to a well-formed wasm module', () => {
  assert.equal(bytes.length, WASM_BYTE_LENGTH);
  assert.deepEqual(bytes.subarray(0, 4), new Uint8Array([0x00, 0x61, 0x73, 0x6d]));
  assert.deepEqual(bytes.subarray(4, 8), new Uint8Array([0x01, 0x00, 0x00, 0x00]));
});

test('the module needs no imports at all', () => {
  // The wasm counterpart of `make size-nolibc`, and it means no import object.
  assert.deepEqual(WebAssembly.Module.imports(cobs.wasmModule), []);
  new WebAssembly.Instance(cobs.wasmModule);   // would throw if an import were needed
});

test('the module exports exactly the ABI the wrapper expects', () => {
  const got = WebAssembly.Module.exports(cobs.wasmModule)
    .map(e => `${e.name}:${e.kind}`).sort();
  assert.deepEqual(got, [
    'cobs_wasm_abi_version:function',
    'cobs_wasm_decode:function',
    'cobs_wasm_encode:function',
    'cobs_wasm_heap_base:function',
    'cobs_wasm_nop:function',
    'memory:memory',
  ]);
});

test('the ABI version in the generated module matches the compiled one', () => {
  const ex = new WebAssembly.Instance(cobs.wasmModule).exports;
  assert.equal(ex.cobs_wasm_abi_version(), WASM_ABI_VERSION);
});

test('the arena base is nonzero, so a zero-length payload is not a null pointer', () => {
  // cobs_encode rejects a null src even at len 0 (cobs.c:211); the shadow stack
  // sits below the arena, which keeps that unreachable.
  const ex = new WebAssembly.Instance(cobs.wasmModule).exports;
  assert.ok(ex.cobs_wasm_heap_base() > 0);
  assert.deepEqual(cobs.encode(new Uint8Array(0)), new Uint8Array([0x01, 0x00]));
});

test('the module fits the synchronous-compile budget', () => {
  // Browsers throw on sync compilation above 4 KiB. Node does not, but staying
  // under it keeps the eager sync path portable.
  assert.ok(bytes.length <= 4096, `${bytes.length} bytes exceeds the 4 KiB budget`);
});

test('cobs_wasm_decode takes the out_consumed pointer', () => {
  // Arity is part of the ABI; a stale wasm would pass the arena base as dst_cap.
  const ex = new WebAssembly.Instance(cobs.wasmModule).exports;
  assert.equal(ex.cobs_wasm_decode.length, 5);
  assert.equal(ex.cobs_wasm_encode.length, 4);
});

test('createCodec accepts a caller-supplied module', () => {
  const codec = cobs.createCodec({ module: new WebAssembly.Module(bytes) });
  assert.deepEqual(codec.decode(codec.encode(new Uint8Array([1, 0, 2]))),
                   new Uint8Array([1, 0, 2]));
});
