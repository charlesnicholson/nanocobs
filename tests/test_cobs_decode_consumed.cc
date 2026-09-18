#include "../cobs.h"
#include "byte_vec.h"
#include "cobs_ref.h"
#include "cobs_shapes.h"
#include "doctest_wrapper.h"
#include "guarded_buf.h"

#include <numeric>
#include <string>

// cobs_decode's out_enc_consumed: the bytes the frame occupied, delimiter included,
// so a caller can walk several frames without scanning. Nullable, set only on success.

namespace {

byte_t constexpr kPoison = 0xCD;

byte_vec_t encode_ok(byte_vec_t const& dec) {
  byte_vec_t enc(COBS_ENCODE_MAX(dec.size()));
  size_t len{ 0u };
  byte_t dummy{ 0 };
  REQUIRE(cobs_encode(dec.empty() ? &dummy : dec.data(),
                      dec.size(),
                      enc.data(),
                      enc.size(),
                      &len) == COBS_RET_SUCCESS);
  enc.resize(len);
  return enc;
}

// Decode the frame at the front of |buf| and return the consumed count.
size_t decode_front(byte_vec_t const& buf, byte_vec_t& out) {
  out.assign(buf.size() + 8u, kPoison);
  size_t dec_len{ 0u }, consumed{ SIZE_MAX };
  REQUIRE(cobs_decode(buf.data(), buf.size(), out.data(), out.size(), &dec_len,
                      &consumed) == COBS_RET_SUCCESS);
  out.resize(dec_len);
  return consumed;
}

}  // namespace

TEST_CASE("Consumed: a lone frame consumes exactly its own length") {
  size_t n_shapes{ 0u };
  cobs_test::shape const* shapes = cobs_test::all_shapes(&n_shapes);

  for (size_t si{ 0 }; si < n_shapes; ++si) {
    for (size_t n{ 0 }; n <= 600; ++n) {
      byte_vec_t const dec = cobs_test::make_shape(shapes[si], n, uint32_t(n + 1u));
      byte_vec_t const enc = encode_ok(dec);
      byte_vec_t out;
      size_t const consumed = decode_front(enc, out);
      REQUIRE_MESSAGE(consumed == enc.size(),
                      cobs_test::shape_name(shapes[si]) << " n=" << n);
      REQUIRE(out == dec);
    }
  }
}

TEST_CASE("Consumed: null is accepted and changes nothing else") {
  // Passing null must be indistinguishable from passing a pointer, in return code,
  // length, and bytes, across the whole corpus.
  size_t n_shapes{ 0u };
  cobs_test::shape const* shapes = cobs_test::all_shapes(&n_shapes);

  for (size_t si{ 0 }; si < n_shapes; ++si) {
    for (size_t n{ 0 }; n <= 600; ++n) {
      byte_vec_t const dec = cobs_test::make_shape(shapes[si], n, uint32_t(n + 11u));
      byte_vec_t const enc = encode_ok(dec);

      byte_vec_t with(n + 8u, kPoison), without(n + 8u, kPoison);
      size_t len_with{ SIZE_MAX }, len_without{ SIZE_MAX }, consumed{ SIZE_MAX };

      cobs_ret_t const r_with = cobs_decode(
          enc.data(), enc.size(), with.data(), with.size(), &len_with, &consumed);
      cobs_ret_t const r_without = cobs_decode(
          enc.data(), enc.size(), without.data(), without.size(), &len_without, nullptr);

      REQUIRE_MESSAGE(r_with == r_without,
                      cobs_test::shape_name(shapes[si]) << " n=" << n);
      REQUIRE(r_with == COBS_RET_SUCCESS);
      REQUIRE(len_with == len_without);
      REQUIRE(with == without);          // including the poison past the payload
      REQUIRE(len_with == n);
      REQUIRE(consumed == enc.size());
    }
  }
}

TEST_CASE("Consumed: null is accepted on every failure path too") {
  // A null out-param must not crash, and must not change which failure it is.
  byte_t dec[64];
  std::vector<std::pair<byte_vec_t, size_t>> const bad = {
    { {}, sizeof(dec) },
    { { 0x01 }, sizeof(dec) },
    { { 0x00, 0x00 }, sizeof(dec) },
    { { 0x03, 0x00 }, sizeof(dec) },
    { { 0x03, 0x00 }, 0 },
    { { 0x09, 0x09 }, sizeof(dec) },
    { { 0x02, 0x01 }, sizeof(dec) },
    { { 0x04, 0x01, 0x00, 0x03, 0x00 }, sizeof(dec) },
    { { 0x05, 0x01, 0x00, 0x00, 0x01, 0x00 }, sizeof(dec) },
    { { 0xFF, 0x01, 0x00 }, sizeof(dec) },
    { { 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 }, 2 },
  };

  for (auto const& [enc, dec_max] : bad) {
    byte_t probe{ 0 };
    byte_t const* const src = enc.empty() ? &probe : enc.data();
    size_t len_with{ 0u }, len_without{ 0u }, consumed{ 0xA5A5A5A5u };

    cobs_ret_t const r_with =
        cobs_decode(src, enc.size(), dec, dec_max, &len_with, &consumed);
    cobs_ret_t const r_without =
        cobs_decode(src, enc.size(), dec, dec_max, &len_without, nullptr);

    REQUIRE(r_with != COBS_RET_SUCCESS);
    REQUIRE(r_with == r_without);
    REQUIRE(consumed == 0xA5A5A5A5u);  // untouched, as documented
  }
}

TEST_CASE("Consumed: walking a buffer of back-to-back frames") {
  // The point of the parameter: no delimiter scanning by the caller.
  size_t n_shapes{ 0u };
  cobs_test::shape const* shapes = cobs_test::all_shapes(&n_shapes);

  for (size_t si{ 0 }; si < n_shapes; ++si) {
    std::vector<byte_vec_t> payloads;
    byte_vec_t stream;
    for (size_t n : { size_t{ 0 }, size_t{ 1 }, size_t{ 7 }, size_t{ 8 }, size_t{ 9 },
                      size_t{ 253 }, size_t{ 254 }, size_t{ 255 }, size_t{ 256 },
                      size_t{ 600 } }) {
      byte_vec_t const dec = cobs_test::make_shape(shapes[si], n, uint32_t(n + 3u));
      payloads.push_back(dec);
      byte_vec_t const enc = encode_ok(dec);
      stream.insert(std::end(stream), std::begin(enc), std::end(enc));
    }

    size_t offset{ 0u };
    for (size_t i{ 0 }; i < payloads.size(); ++i) {
      byte_vec_t const remaining(std::begin(stream) + long(offset), std::end(stream));
      byte_vec_t out;
      size_t const consumed = decode_front(remaining, out);
      REQUIRE_MESSAGE(out == payloads[i],
                      cobs_test::shape_name(shapes[si]) << " frame " << i);
      offset += consumed;
    }
    REQUIRE(offset == stream.size());  // landed exactly on the end
  }
}

TEST_CASE("Consumed: trailing bytes after the frame are not consumed") {
  for (size_t n : { size_t{ 0 }, size_t{ 1 }, size_t{ 9 }, size_t{ 254 }, size_t{ 255 } }) {
    byte_vec_t const dec(n, 0x41);
    byte_vec_t const enc = encode_ok(dec);

    for (size_t extra{ 1 }; extra <= 8; ++extra) {
      for (byte_t const filler : { byte_t{ 0x00 }, byte_t{ 0x01 }, byte_t{ 0xFF } }) {
        byte_vec_t buf = enc;
        buf.insert(std::end(buf), extra, filler);
        byte_vec_t out;
        size_t const consumed = decode_front(buf, out);
        REQUIRE_MESSAGE(consumed == enc.size(), "n=" << n << " extra=" << extra);
        REQUIRE(out == dec);
      }
    }
  }
}

TEST_CASE("Consumed: untouched when the call fails") {
  byte_t dec[64];
  size_t dec_len{ 0u };

  auto fails = [&](byte_vec_t const& enc, size_t dec_max, cobs_ret_t expect) {
    size_t consumed{ 0xA5A5A5A5u };
    byte_t probe{ 0 };
    REQUIRE(cobs_decode(enc.empty() ? &probe : enc.data(),
                        enc.size(),
                        dec,
                        dec_max,
                        &dec_len,
                        &consumed) == expect);
    REQUIRE(consumed == 0xA5A5A5A5u);  // sentinel survived
  };

  SUBCASE("BAD_ARG on a short input") {
    fails({}, sizeof(dec), COBS_RET_ERR_BAD_ARG);
    fails({ 0x01 }, sizeof(dec), COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("BAD_PAYLOAD") {
    fails({ 0x05, 0x01, 0x00, 0x00, 0x01, 0x00 }, sizeof(dec), COBS_RET_ERR_BAD_PAYLOAD);
    fails({ 0x04, 0x01, 0x00, 0x03, 0x00 }, sizeof(dec), COBS_RET_ERR_BAD_PAYLOAD);
    // A code byte past the end reads the delimiter as an interior zero, so with room
    // to write this is BAD_PAYLOAD.
    fails({ 0x03, 0x00 }, sizeof(dec), COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("EXHAUSTED: no terminating delimiter") {
    fails({ 0x02, 0x01 }, sizeof(dec), COBS_RET_ERR_EXHAUSTED);
  }

  SUBCASE("The same input can give either code, depending on dec_max") {
    // cobs.c:632-634 checks both bounds before reading, so a full output buffer says
    // EXHAUSTED where a roomy one says BAD_PAYLOAD. Either way consumed is untouched.
    fails({ 0x03, 0x00 }, 0, COBS_RET_ERR_EXHAUSTED);
    fails({ 0x03, 0x00 }, sizeof(dec), COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("EXHAUSTED: output buffer too small") {
    fails({ 0x05, 0x11, 0x22, 0x33, 0x44, 0x00 }, 2, COBS_RET_ERR_EXHAUSTED);
  }

  SUBCASE("Null pointers") {
    size_t consumed{ 0xA5A5A5A5u };
    byte_vec_t const enc{ 0x01, 0x00 };
    REQUIRE(cobs_decode(nullptr, enc.size(), dec, sizeof(dec), &dec_len, &consumed) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode(enc.data(), enc.size(), nullptr, sizeof(dec), &dec_len,
                        &consumed) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_decode(enc.data(), enc.size(), dec, sizeof(dec), nullptr, &consumed) ==
            COBS_RET_ERR_BAD_ARG);
    REQUIRE(consumed == 0xA5A5A5A5u);
  }
}

TEST_CASE("Consumed: agrees with the byte-loop build") {
  // The SWAR lane advances src_idx in word strides, so this is what it could get
  // wrong by a few bytes.
  size_t n_shapes{ 0u };
  cobs_test::shape const* shapes = cobs_test::all_shapes(&n_shapes);

  for (size_t si{ 0 }; si < n_shapes; ++si) {
    for (size_t n{ 0 }; n <= 600; ++n) {
      byte_vec_t const dec = cobs_test::make_shape(shapes[si], n, uint32_t(n + 5u));
      byte_vec_t const enc = encode_ok(dec);

      byte_vec_t a(dec.size() + 8u, kPoison), b(dec.size() + 8u, kPoison);
      size_t la{ 0u }, lb{ 0u }, ca{ SIZE_MAX }, cb{ SIZE_MAX };
      cobs_ret_t const ra =
          cobs_ref_decode(enc.data(), enc.size(), a.data(), a.size(), &la, &ca);
      cobs_ret_t const rb =
          cobs_decode(enc.data(), enc.size(), b.data(), b.size(), &lb, &cb);

      REQUIRE(ra == rb);
      REQUIRE(la == lb);
      REQUIRE_MESSAGE(ca == cb,
                      cobs_test::shape_name(shapes[si]) << " n=" << n << " ref=" << ca
                                                        << " swar=" << cb);
      REQUIRE(ca == enc.size());
    }
  }
}

TEST_CASE("Consumed: every 0xFF-block boundary and word phase") {
  // A forced 0xFF block emits no zero, so the decoder crosses it with no delimiter
  // to key on.
  for (size_t lead{ 0 }; lead <= 9; ++lead) {
    for (size_t run : { size_t{ 252 }, size_t{ 253 }, size_t{ 254 }, size_t{ 255 },
                        size_t{ 256 }, size_t{ 507 }, size_t{ 508 }, size_t{ 509 },
                        size_t{ 510 }, size_t{ 762 }, size_t{ 763 } }) {
      byte_vec_t dec(lead, 0x00);
      dec.insert(std::end(dec), run, 0x41);
      dec.insert(std::end(dec), lead, 0x00);

      byte_vec_t const enc = encode_ok(dec);
      byte_vec_t out;
      REQUIRE_MESSAGE(decode_front(enc, out) == enc.size(),
                      "lead=" << lead << " run=" << run);
      REQUIRE(out == dec);
    }
  }
}

TEST_CASE("Consumed: the guard page still bounds the output") {
  // The parameter must not change how far decode writes. The guard page proves it.
  for (size_t n : { size_t{ 0 }, size_t{ 1 }, size_t{ 8 }, size_t{ 9 }, size_t{ 254 },
                    size_t{ 255 }, size_t{ 256 }, size_t{ 600 } }) {
    for (size_t ofs{ 0 }; ofs <= 8; ++ofs) {
      byte_vec_t dec(n);
      for (size_t i{ 0 }; i < n; ++i) {
        dec[i] = ((i % 9u) == 8u) ? byte_t{ 0 } : byte_t(1u + (i % 255u));
      }
      byte_vec_t const enc = encode_ok(dec);

      cobs_test::guarded_buf gb{ n, ofs };
      size_t dec_len{ 0u }, consumed{ SIZE_MAX };
      REQUIRE(cobs_decode(enc.data(), enc.size(), gb.data(), n, &dec_len, &consumed) ==
              COBS_RET_SUCCESS);
      REQUIRE(dec_len == n);
      REQUIRE(consumed == enc.size());
      REQUIRE(byte_vec_t(gb.data(), gb.data() + n) == dec);
    }
  }
}
