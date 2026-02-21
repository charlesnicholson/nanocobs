#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <algorithm>
#include <cstring>
#include <numeric>

static constexpr byte_t CSV = COBS_TINYFRAME_SENTINEL_VALUE;

namespace {
cobs_ret_t cobs_decode_vec(byte_vec_t& v) {
  return cobs_decode_tinyframe(v.data(), static_cast<unsigned>(v.size()));
}

// Encode in-place, then decode in-place, verify payload matches original.
void round_trip(byte_vec_t const& payload) {
  byte_vec_t buf;
  buf.push_back(CSV);
  buf.insert(buf.end(), payload.begin(), payload.end());
  buf.push_back(CSV);

  REQUIRE(cobs_encode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);

  // Encoded frame: no interior zeros, ends with 0x00
  REQUIRE(buf.back() == 0x00);
  REQUIRE(std::none_of(buf.data(), buf.data() + buf.size() - 1, [](byte_t b) {
    return b == 0;
  }));

  REQUIRE(cobs_decode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);

  // Sentinels restored
  REQUIRE(buf.front() == CSV);
  REQUIRE(buf.back() == CSV);

  // Payload matches
  REQUIRE(byte_vec_t(buf.begin() + 1, buf.end() - 1) == payload);
}
}  // namespace

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

TEST_CASE("Tinyframe decode: bad args") {
  SUBCASE("Null buffer pointer") {
    REQUIRE(cobs_decode_tinyframe(nullptr, 123) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Length 0") {
    char buf;
    REQUIRE(cobs_decode_tinyframe(&buf, 0) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Length 1") {
    char buf;
    REQUIRE(cobs_decode_tinyframe(&buf, 1) == COBS_RET_ERR_BAD_ARG);
  }
}

TEST_CASE("Tinyframe decode: bad payload") {
  SUBCASE("Starts with 0x00") {
    byte_vec_t buf{ 0x00, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Does not end with 0x00") {
    byte_vec_t buf{ 0x01, 0x01 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Second code byte jumps past end") {
    byte_vec_t buf{ 0x01, 0x02, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("First code byte jumps past end") {
    byte_vec_t buf{ 0x03, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte lands on an interior 0x00") {
    byte_vec_t buf{ 0x01, 0x00, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte jumps over an interior 0x00") {
    byte_vec_t buf{ 0x02, 0x00, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte jumps over interior 0x00 in longer frame") {
    byte_vec_t buf{ 0x04, 0x01, 0x00, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte 0xFF jumps well past end") {
    byte_vec_t buf{ 0xFF, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte 0x05 jumps past end") {
    byte_vec_t buf{ 0x05, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code byte 0x0A with only 3 data bytes") {
    byte_vec_t buf{ 0x0A, 0x01, 0x02, 0x03, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code chain does not land exactly on the final byte") {
    // Valid code bytes but they overshoot or undershoot the delimiter
    byte_vec_t buf{ 0x02, 0x11, 0x03, 0x22, 0x33, 0x00, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Code chain lands before final byte") {
    byte_vec_t buf{ 0x01, 0x01, 0x01, 0x00, 0x00 };
    // Codes: 0x01 → pos 1, 0x01 → pos 2, 0x01 → pos 3 (the 0x00). Done.
    // But frame is 5 bytes, delimiter should be at pos 4.
    // cur=3 != len-1=4, so BAD_PAYLOAD.
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }
}

// ---------------------------------------------------------------------------
// Known-vector decodings
// ---------------------------------------------------------------------------

TEST_CASE("Tinyframe decode: known vectors") {
  SUBCASE("Empty payload") {
    byte_vec_t buf{ 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, CSV });
  }

  SUBCASE("One nonzero byte") {
    byte_vec_t buf{ 0x02, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x01, CSV });
  }

  SUBCASE("One zero byte") {
    byte_vec_t buf{ 0x01, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x00, CSV });
  }

  SUBCASE("Two nonzero bytes") {
    byte_vec_t buf{ 0x03, 0x11, 0x22, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x11, 0x22, CSV });
  }

  SUBCASE("Two zero bytes") {
    byte_vec_t buf{ 0x01, 0x01, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x00, 0x00, CSV });
  }

  SUBCASE("Nonzero, zero, nonzero") {
    byte_vec_t buf{ 0x02, 0x11, 0x02, 0x22, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x11, 0x00, 0x22, CSV });
  }

  SUBCASE("Zero, nonzero, zero") {
    byte_vec_t buf{ 0x01, 0x02, 0x42, 0x01, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x00, 0x42, 0x00, CSV });
  }

  SUBCASE("Payload containing the sentinel value 0x5A") {
    byte_vec_t buf{ 0x03, 0x5A, 0x5A, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0x5A, 0x5A, CSV });
  }

  SUBCASE("All 0xFF bytes (max nonzero value)") {
    byte_vec_t buf{ 0x04, 0xFF, 0xFF, 0xFF, 0x00 };
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ CSV, 0xFF, 0xFF, 0xFF, CSV });
  }
}

// ---------------------------------------------------------------------------
// Boundary cases
// ---------------------------------------------------------------------------

TEST_CASE("Tinyframe decode: safe buffer all zeros") {
  byte_vec_t buf(COBS_TINYFRAME_SAFE_BUFFER_SIZE);
  std::fill(buf.begin(), buf.end(), byte_t{ 0x01 });
  buf.back() = 0x00;
  REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);

  byte_vec_t expected(buf.size(), 0x00);
  expected.front() = CSV;
  expected.back() = CSV;
  REQUIRE(buf == expected);
}

TEST_CASE("Tinyframe decode: safe buffer no zeros") {
  byte_vec_t buf(COBS_TINYFRAME_SAFE_BUFFER_SIZE);
  std::iota(buf.begin(), buf.end(), byte_t{ 0x00 });
  buf[0] = 0xFF;
  buf.back() = 0x00;
  REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);

  byte_vec_t expected(buf.size());
  std::iota(expected.begin(), expected.end(), byte_t{ 0x00 });
  expected.front() = CSV;
  expected.back() = CSV;
  REQUIRE(buf == expected);
}

TEST_CASE("Tinyframe decode: unsafe payload with 254-byte jumps") {
  SUBCASE("Three 0xFF code blocks") {
    byte_vec_t buf{ 0xFF };
    buf.insert(buf.end(), 254, 0x01);
    buf.push_back(0xFF);
    buf.insert(buf.end(), 254, 0x01);
    buf.push_back(0xFF);
    buf.insert(buf.end(), 254, 0x01);
    buf.push_back(0x00);
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);

    byte_vec_t expected{ CSV };
    expected.insert(expected.end(), 254, 0x01);
    expected.push_back(0x00);
    expected.insert(expected.end(), 254, 0x01);
    expected.push_back(0x00);
    expected.insert(expected.end(), 254, 0x01);
    expected.push_back(CSV);
    REQUIRE(buf == expected);
  }

  SUBCASE("One 0xFF code block") {
    byte_vec_t buf{ 0xFF };
    buf.insert(buf.end(), 254, 0x42);
    buf.push_back(0x00);
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);

    REQUIRE(buf.front() == CSV);
    REQUIRE(buf.back() == CSV);
    for (size_t i{ 1 }; i <= 254; ++i) {
      REQUIRE(buf[i] == 0x42);
    }
  }

  SUBCASE("0xFF block followed by short block") {
    byte_vec_t buf{ 0xFF };
    buf.insert(buf.end(), 254, 0x01);
    buf.push_back(0x03);
    buf.push_back(0xAA);
    buf.push_back(0xBB);
    buf.push_back(0x00);
    REQUIRE(cobs_decode_vec(buf) == COBS_RET_SUCCESS);

    REQUIRE(buf.front() == CSV);
    REQUIRE(buf.back() == CSV);
    // After the 0xFF block: implicit zero, then 0xAA, 0xBB
    REQUIRE(buf[255] == 0x00);
    REQUIRE(buf[256] == 0xAA);
    REQUIRE(buf[257] == 0xBB);
  }
}

// ---------------------------------------------------------------------------
// Round-trip tests
// ---------------------------------------------------------------------------

TEST_CASE("Tinyframe decode: round-trip small payloads") {
  SUBCASE("Empty") {
    round_trip({});
  }
  SUBCASE("Single zero") {
    round_trip({ 0x00 });
  }
  SUBCASE("Single nonzero") {
    round_trip({ 0x42 });
  }
  SUBCASE("Single 0xFF") {
    round_trip({ 0xFF });
  }
  SUBCASE("Sentinel value as data") {
    round_trip({ CSV });
  }
  SUBCASE("Two sentinels as data") {
    round_trip({ CSV, CSV });
  }
  SUBCASE("Zero then sentinel") {
    round_trip({ 0x00, CSV });
  }
  SUBCASE("Sentinel then zero") {
    round_trip({ CSV, 0x00 });
  }
  SUBCASE("Three zeros") {
    round_trip({ 0x00, 0x00, 0x00 });
  }
  SUBCASE("Three nonzeros") {
    round_trip({ 0x11, 0x22, 0x33 });
  }
  SUBCASE("Alternating") {
    round_trip({ 0x00, 0x11, 0x00, 0x22, 0x00 });
  }
}

TEST_CASE("Tinyframe decode: round-trip at all safe lengths") {
  SUBCASE("Fill with zeros") {
    for (size_t i{ 0 }; i <= COBS_TINYFRAME_SAFE_BUFFER_SIZE - 2; ++i) {
      round_trip(byte_vec_t(i, 0x00));
    }
  }

  SUBCASE("Fill with nonzeros") {
    for (size_t i{ 0 }; i <= COBS_TINYFRAME_SAFE_BUFFER_SIZE - 2; ++i) {
      round_trip(byte_vec_t(i, 0x01));
    }
  }

  SUBCASE("Fill with 0xFF") {
    for (size_t i{ 0 }; i <= COBS_TINYFRAME_SAFE_BUFFER_SIZE - 2; ++i) {
      round_trip(byte_vec_t(i, 0xFF));
    }
  }

  SUBCASE("Ascending bytes") {
    for (size_t i{ 0 }; i <= COBS_TINYFRAME_SAFE_BUFFER_SIZE - 2; ++i) {
      byte_vec_t payload(i);
      for (size_t j{ 0 }; j < i; ++j) {
        payload[j] = static_cast<byte_t>(j);
      }
      round_trip(payload);
    }
  }
}

// ---------------------------------------------------------------------------
// Inplace decode == External decode
// ---------------------------------------------------------------------------

namespace {
void verify_decode_inplace(unsigned char* inplace, size_t payload_len) {
  byte_vec_t external(std::max(payload_len, size_t(1)));
  size_t external_len{ 0u };
  REQUIRE_MESSAGE(cobs_decode(inplace,
                              payload_len + 2,
                              external.data(),
                              external.size(),
                              &external_len) == COBS_RET_SUCCESS,
                  payload_len);

  REQUIRE(external_len == payload_len);
  REQUIRE(cobs_decode_tinyframe(inplace, payload_len + 2) == COBS_RET_SUCCESS);
  REQUIRE(byte_vec_t(inplace + 1, inplace + external_len + 1) ==
          byte_vec_t(external.data(), external.data() + external_len));
}

void fill_encode_inplace(byte_t* inplace, size_t payload_len, byte_t f) {
  inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
  memset(inplace + 1, f, payload_len);
  inplace[payload_len + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
  REQUIRE_MESSAGE(cobs_encode_tinyframe(inplace, payload_len + 2) == COBS_RET_SUCCESS,
                  payload_len);
}
}  // namespace

TEST_CASE("Tinyframe decode: inplace == external") {
  std::array<byte_t, COBS_TINYFRAME_SAFE_BUFFER_SIZE> inplace;

  SUBCASE("Fill with zeros") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      fill_encode_inplace(inplace.data(), i, 0x00);
      verify_decode_inplace(inplace.data(), i);
    }
  }

  SUBCASE("Fill with nonzeros") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      fill_encode_inplace(inplace.data(), i, 0x01);
      verify_decode_inplace(inplace.data(), i);
    }
  }

  SUBCASE("Fill with 0xFF") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      fill_encode_inplace(inplace.data(), i, 0xFF);
      verify_decode_inplace(inplace.data(), i);
    }
  }

  SUBCASE("Fill with zero/one pattern") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
      for (auto j{ 1u }; j < i; ++j) {
        inplace[j] = j & 1;
      }
      inplace[i + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
      REQUIRE(cobs_encode_tinyframe(inplace.data(), i + 2) == COBS_RET_SUCCESS);
      verify_decode_inplace(inplace.data(), i);
    }
  }

  SUBCASE("Fill with one/zero pattern") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
      for (auto j{ 1u }; j < i; ++j) {
        inplace[j] = (j & 1) ^ 1;
      }
      inplace[i + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
      REQUIRE(cobs_encode_tinyframe(inplace.data(), i + 2) == COBS_RET_SUCCESS);
      verify_decode_inplace(inplace.data(), i);
    }
  }

  SUBCASE("Fill with sentinel value") {
    for (auto i{ 0u }; i < inplace.size() - 2; ++i) {
      fill_encode_inplace(inplace.data(), i, CSV);
      verify_decode_inplace(inplace.data(), i);
    }
  }
}
