// SPDX-License-Identifier: Unlicense OR 0BSD
//
// Golden vectors for the npm package's tests, produced by the C itself. Pins the JS
// to cobs.c's exact bytes and error codes.
//
// Payloads are inline, not a shape+seed pair: std::mt19937 is not reproducible from
// JavaScript. Writes JSON to stdout; `make js-vectors` puts it where the tests look.

#include "cobs.h"
#include "cobs_shapes.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string b64(byte_t const* p, size_t n) {
  static char const* tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((n + 2) / 3) * 4);
  for (size_t i = 0; i < n; i += 3) {
    unsigned const a = p[i];
    unsigned const b = (i + 1 < n) ? p[i + 1] : 0u;
    unsigned const c = (i + 2 < n) ? p[i + 2] : 0u;
    unsigned const v = (a << 16) | (b << 8) | c;
    out += tbl[(v >> 18) & 0x3Fu];
    out += tbl[(v >> 12) & 0x3Fu];
    out += (i + 1 < n) ? tbl[(v >> 6) & 0x3Fu] : '=';
    out += (i + 2 < n) ? tbl[v & 0x3Fu] : '=';
  }
  return out;
}

// 8/9 and 16/17 straddle the 64-bit SWAR word; 253-257 and 508-511 straddle the
// 254-byte block limit where code == 0xFF lives.
size_t const kLengths[] = { 0,   1,   2,   3,   7,   8,   9,   15,  16,  17,
                            31,  32,  33,  63,  64,  127, 128, 253, 254, 255,
                            256, 257, 508, 509, 510, 511, 1024, 2048 };

char const* ret_name(cobs_ret_t r) {
  switch (r) {
    case COBS_RET_SUCCESS: return "SUCCESS";
    case COBS_RET_ERR_BAD_ARG: return "BAD_ARG";
    case COBS_RET_ERR_BAD_PAYLOAD: return "BAD_PAYLOAD";
    case COBS_RET_ERR_EXHAUSTED: return "EXHAUSTED";
  }
  return "?";
}

bool first = true;
void comma() {
  if (!first) { std::fputs(",\n", stdout); }
  first = false;
}

}  // namespace

int main() {
  std::fputs("{\n\"encode\": [\n", stdout);

  size_t shape_count = 0;
  cobs_test::shape const* shapes = cobs_test::all_shapes(&shape_count);

  for (size_t si = 0; si < shape_count; ++si) {
    for (size_t li = 0; li < sizeof(kLengths) / sizeof(kLengths[0]); ++li) {
      size_t const n = kLengths[li];
      byte_vec_t payload = cobs_test::make_shape(shapes[si], n, 1u + static_cast<uint32_t>(li));

      byte_vec_t frame(COBS_ENCODE_MAX(n));
      size_t enc_len = 0;
      // cobs_encode rejects a null src even at n == 0, so an empty payload still
      // needs a valid pointer; frame's storage is as good as any.
      byte_t const* const src = payload.empty() ? frame.data() : payload.data();
      cobs_ret_t const r =
          cobs_encode(src, n, frame.data(), frame.size(), &enc_len);
      if (r != COBS_RET_SUCCESS) {
        std::fprintf(stderr, "encode failed unexpectedly: %s shape=%s n=%zu\n",
                     ret_name(r), cobs_test::shape_name(shapes[si]), n);
        return 1;
      }

      // Decode it back so the vector carries cobs_decode's out_enc_consumed too.
      byte_vec_t back(n + 2u);
      size_t dec_len = 0, consumed = 0;
      cobs_ret_t const dr = cobs_decode(frame.data(), enc_len, back.data(),
                                        back.size(), &dec_len, &consumed);
      if ((dr != COBS_RET_SUCCESS) || (dec_len != n) || (consumed != enc_len)) {
        std::fprintf(stderr,
                     "decode disagreed: %s shape=%s n=%zu dec_len=%zu consumed=%zu "
                     "enc_len=%zu\n",
                     ret_name(dr), cobs_test::shape_name(shapes[si]), n, dec_len,
                     consumed, enc_len);
        return 1;
      }

      comma();
      std::printf("  {\"shape\": \"%s\", \"len\": %zu, \"payload\": \"%s\", "
                  "\"frame\": \"%s\", \"consumed\": %zu}",
                  cobs_test::shape_name(shapes[si]), n,
                  b64(src, payload.size()).c_str(),
                  b64(frame.data(), enc_len).c_str(), consumed);
    }
  }

  // Malformed inputs, recording whatever cobs_decode returns rather than asserting.
  std::fputs("\n],\n\"decode_errors\": [\n", stdout);
  first = true;

  std::vector<byte_vec_t> const bad = {
    {},                                  // enc_len 0
    { 0x01 },                            // enc_len 1
    { 0x00, 0x00 },                      // leading delimiter
    { 0x03, 0x00 },                      // code byte points past the end
    { 0x09, 0x09 },                      // no trailing delimiter
    { 0x03, 0x11, 0x00, 0x00 },          // interior zero inside a block
    { 0x05, 0x01, 0x00, 0x00, 0x01, 0x00 },
    { 0x04, 0x01, 0x00, 0x03, 0x00 },
    { 0xFF, 0x01, 0x00 },                // 0xFF block with nothing behind it
    { 0x02, 0x11, 0x22, 0x00 },          // block shorter than its code claims
  };

  for (auto const& enc : bad) {
    byte_vec_t out(enc.size() + 2u);
    size_t dec_len = 0;
    byte_t probe = 0;
    byte_t const* const src = enc.empty() ? &probe : enc.data();
    cobs_ret_t const r =
        cobs_decode(src, enc.size(), out.data(), out.size(), &dec_len, nullptr);
    comma();
    std::printf("  {\"frame\": \"%s\", \"ret\": \"%s\"}",
                b64(enc.data(), enc.size()).c_str(), ret_name(r));
  }

  std::fputs("\n]\n}\n", stdout);
  return 0;
}
