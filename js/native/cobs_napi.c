// SPDX-License-Identifier: Unlicense OR 0BSD
//
// The native backend for the npm package: cobs.c behind an N-API surface that matches
// js/src/index.js call for call, so the two are interchangeable. Which one a caller
// gets is js/src/node.js's decision; js/test/conformance.test.mjs is what keeps them
// honest, by running both over the same corpus and requiring identical bytes and
// identical error codes.
//
// Native's advantage over wasm is entirely that it reads and writes the caller's
// Uint8Array where it already lives. Every entry point here takes typed arrays and
// touches their backing store directly; nothing is copied to reach the codec.
//
// Tinyframe is deliberately absent -- the JS package has never exposed it.
//
// Returns >= 0 for a byte count, or -(cobs_ret_t): -1 BAD_ARG, -2 BAD_PAYLOAD,
// -3 EXHAUSTED. The JS wrapper turns a negative into a CobsError, so the throwing and
// non-throwing forms share one path, as they do on the wasm side.

#include <node_api.h>

#include "cobs.h"

#define NAPI_CALL(env, call)       \
  do {                             \
    if ((call) != napi_ok) {       \
      return NULL;                 \
    }                              \
  } while (0)

// Same contract as the JS wrapper's assertBytes: a Uint8Array, or a TypeError. Buffer
// and a cross-realm Uint8Array both pass, because napi_get_typedarray_info looks at the
// value rather than at a constructor identity.
static bool as_u8(napi_env env, napi_value v, cobs_byte_t** data, size_t* len) {
  napi_typedarray_type type;
  size_t length = 0, offset = 0;
  void* d = NULL;
  napi_value ab;
  if (napi_get_typedarray_info(env, v, &type, &length, &d, &ab, &offset) != napi_ok) {
    return false;
  }
  if (type != napi_uint8_array) {
    return false;
  }
  // A zero-length typed array hands back a null data pointer, and cobs_encode and
  // cobs_decode reject a null buffer even at length 0. The wasm backend never meets
  // this -- its pointers are linear-memory offsets and its arena base is nonzero on
  // purpose -- so point at a real byte and keep the two answers identical. Nothing is
  // ever read or written through it: the length that goes with it is 0.
  static cobs_byte_t empty;
  *data = d ? (cobs_byte_t*)d : &empty;  // d is already adjusted by byte_offset
  *len = length;
  return true;
}

static bool as_u32(napi_env env, napi_value v, uint32_t** data, size_t* len) {
  napi_typedarray_type type;
  size_t length = 0, offset = 0;
  void* d = NULL;
  napi_value ab;
  if (napi_get_typedarray_info(env, v, &type, &length, &d, &ab, &offset) != napi_ok) {
    return false;
  }
  if (type != napi_uint32_array) {
    return false;
  }
  *data = (uint32_t*)d;
  *len = length;
  return true;
}

static napi_value type_error(napi_env env, char const* msg) {
  napi_throw_type_error(env, NULL, msg);
  return NULL;
}

static napi_value i32(napi_env env, int32_t v) {
  napi_value out;
  NAPI_CALL(env, napi_create_int32(env, v, &out));
  return out;
}

static int32_t ret_or_len(cobs_ret_t r, size_t len) {
  if (r != COBS_RET_SUCCESS) {
    return -(int32_t)r;
  }
  // Unreachable; the JS side caps payloads well below this. A length that did not fit
  // the int32 return would read as an error code. This is the return ABI's limit, not
  // a bound on what cobs.c will encode.
  return (len > (size_t)INT32_MAX) ? -(int32_t)COBS_RET_ERR_EXHAUSTED : (int32_t)len;
}

// ------------------------------------------------------------ into: no alloc --

static napi_value js_encode_into(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  cobs_byte_t *src, *dst;
  size_t slen, dlen;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  if ((argc < 2) || !as_u8(env, argv[0], &src, &slen) ||
      !as_u8(env, argv[1], &dst, &dlen)) {
    return type_error(env, "nanocobs: arguments must be Uint8Array");
  }
  // Two statements on purpose: C does not order sibling arguments, so passing the
  // call and |out| together let x86-64 read |out| before cobs_encode filled it in,
  // and every encodeInto returned 0 there while arm64 was fine.
  size_t out = 0;
  cobs_ret_t const r = cobs_encode(src, slen, dst, dlen, &out);
  return i32(env, ret_or_len(r, out));
}

// |consumed| is an optional Uint32Array whose [0] receives cobs_decode's
// out_enc_consumed. A caller-owned slot rather than module state, so this stays
// reentrant like cobs_decode itself.
static napi_value js_decode_into(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  cobs_byte_t *src, *dst;
  size_t slen, dlen;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  if ((argc < 2) || !as_u8(env, argv[0], &src, &slen) ||
      !as_u8(env, argv[1], &dst, &dlen)) {
    return type_error(env, "nanocobs: arguments must be Uint8Array");
  }

  uint32_t* slot = NULL;
  size_t slot_len = 0;
  napi_valuetype t = napi_undefined;
  if (argc >= 3) {
    NAPI_CALL(env, napi_typeof(env, argv[2], &t));
  }
  if ((t != napi_undefined) && (t != napi_null)) {
    if (!as_u32(env, argv[2], &slot, &slot_len) || !slot_len) {
      return type_error(env, "nanocobs: consumed must be a Uint32Array");
    }
  }

  size_t out = 0, consumed = 0;
  cobs_ret_t const r = cobs_decode(src, slen, dst, dlen, &out, slot ? &consumed : NULL);
  if (slot && (r == COBS_RET_SUCCESS)) {
    slot[0] = (uint32_t)consumed;
  }
  return i32(env, ret_or_len(r, out));
}

// -------------------------------------------------------- allocating helpers --
// A Uint8Array view of the exact result length over a buffer sized to the proven
// bound. The codec writes straight into the array that gets handed back, so the
// allocation is the only cost -- there is no copy out the way wasm needs one.

static napi_value alloc_result(napi_env env,
                               size_t cap,
                               cobs_byte_t const* src,
                               size_t slen,
                               bool encode,
                               uint32_t* slot) {
  void* data = NULL;
  napi_value ab;
  NAPI_CALL(env, napi_create_arraybuffer(env, cap ? cap : 1, &data, &ab));

  size_t out = 0, consumed = 0;
  cobs_ret_t const r =
      encode ? cobs_encode(src, slen, data, cap, &out)
             : cobs_decode(src, slen, data, cap, &out, slot ? &consumed : NULL);
  if (r != COBS_RET_SUCCESS) {
    return i32(env, -(int32_t)r);
  }
  if (slot) {
    slot[0] = (uint32_t)consumed;
  }
  napi_value ta;
  NAPI_CALL(env, napi_create_typedarray(env, napi_uint8_array, out, ab, 0, &ta));
  return ta;
}

// Returns a Uint8Array on success or a negative number on failure: one return value
// for both, so the wrapper decides whether to throw without a second call.
static napi_value js_encode(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  cobs_byte_t* src;
  size_t slen;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  if (!argc || !as_u8(env, argv[0], &src, &slen)) {
    return type_error(env, "nanocobs: payload must be a Uint8Array");
  }
  return alloc_result(env, COBS_ENCODE_MAX(slen), src, slen, true, NULL);
}

static napi_value js_decode(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  cobs_byte_t* src;
  size_t slen;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, argv, NULL, NULL));
  if (!argc || !as_u8(env, argv[0], &src, &slen)) {
    return type_error(env, "nanocobs: frame must be a Uint8Array");
  }
  uint32_t* slot = NULL;
  size_t slot_len = 0;
  napi_valuetype t = napi_undefined;
  if (argc >= 2) {
    NAPI_CALL(env, napi_typeof(env, argv[1], &t));
  }
  if ((t != napi_undefined) && (t != napi_null)) {
    if (!as_u32(env, argv[1], &slot, &slot_len) || !slot_len) {
      return type_error(env, "nanocobs: consumed must be a Uint32Array");
    }
  }
  return alloc_result(env, (slen > 2) ? (slen - 2) : 0, src, slen, false, slot);
}

NAPI_MODULE_INIT() {
  napi_property_descriptor const props[] = {
    { "encode", NULL, js_encode, NULL, NULL, NULL, napi_default, NULL },
    { "decode", NULL, js_decode, NULL, NULL, NULL, napi_default, NULL },
    { "encodeInto", NULL, js_encode_into, NULL, NULL, NULL, napi_default, NULL },
    { "decodeInto", NULL, js_decode_into, NULL, NULL, NULL, napi_default, NULL },
  };
  if (napi_define_properties(env, exports, sizeof(props) / sizeof(props[0]), props) !=
      napi_ok) {
    return NULL;
  }
  return exports;
}
