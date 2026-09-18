// SPDX-License-Identifier: Unlicense OR 0BSD

// The wasm ABI for the npm package, not part of the C library. A JS caller makes one
// call per frame and gets the length back instead of reading an out-param.
//
// Returns >= 0 for bytes written, or -(cobs_ret_t): -1 BAD_ARG, -2 BAD_PAYLOAD,
// -3 EXHAUSTED. Pointers are linear-memory offsets, typed uint32_t to say so.

#include "cobs.h"

// Bump on any ABI change, so a caller supplying its own Module can refuse a stale one.
enum { COBS_WASM_ABI = 1 };

#define COBS_WASM_EXPORT(name) \
  __attribute__((export_name(name), used, visibility("default")))

// wasm-ld defines this: the first byte above the shadow stack and data. Everything
// from here up is the JS side's arena; there is no allocator.
extern cobs_byte_t __heap_base;

COBS_WASM_EXPORT("cobs_wasm_abi_version")
uint32_t cobs_wasm_abi_version(void) {
  return (uint32_t)COBS_WASM_ABI;
}

COBS_WASM_EXPORT("cobs_wasm_heap_base")
uint32_t cobs_wasm_heap_base(void) {
  return (uint32_t)(uintptr_t)&__heap_base;
}

COBS_WASM_EXPORT("cobs_wasm_nop")  // measures the JS->wasm boundary in isolation
int32_t cobs_wasm_nop(int32_t x) {
  return x;
}

static int32_t cobs_wasm_result(cobs_ret_t r, size_t len) {
  if (r != COBS_RET_SUCCESS) {
    return -(int32_t)r;
  }
  // Unreachable; the JS side caps payloads. A negative length would read as an error.
  return (len > (size_t)INT32_MAX) ? -(int32_t)COBS_RET_ERR_EXHAUSTED : (int32_t)len;
}

COBS_WASM_EXPORT("cobs_wasm_encode")
int32_t cobs_wasm_encode(uint32_t src, uint32_t src_len, uint32_t dst, uint32_t dst_cap) {
  size_t out = 0;
  cobs_ret_t const r = cobs_encode((cobs_byte_t const*)(uintptr_t)src,
                                   src_len,
                                   (cobs_byte_t*)(uintptr_t)dst,
                                   dst_cap,
                                   &out);
  return cobs_wasm_result(r, out);
}

// |out_consumed| is a linear-memory offset to a uint32_t, or 0 for none: the shadow
// stack sits at offset 0, so no real out-param lands there. A parameter, not shim
// state, so the export stays reentrant like cobs_decode.
COBS_WASM_EXPORT("cobs_wasm_decode")
int32_t cobs_wasm_decode(uint32_t src,
                         uint32_t src_len,
                         uint32_t dst,
                         uint32_t dst_cap,
                         uint32_t out_consumed) {
  size_t out = 0, consumed = 0;
  cobs_ret_t const r = cobs_decode((cobs_byte_t const*)(uintptr_t)src,
                                   src_len,
                                   (cobs_byte_t*)(uintptr_t)dst,
                                   dst_cap,
                                   &out,
                                   out_consumed ? &consumed : NULL);
  if (out_consumed && (r == COBS_RET_SUCCESS)) {
    *(uint32_t*)(uintptr_t)out_consumed = (uint32_t)consumed;
  }
  return cobs_wasm_result(r, out);
}
