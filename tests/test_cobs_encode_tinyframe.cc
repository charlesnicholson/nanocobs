#include "../cobs.h"
#include "byte_vec.h"
#include "doctest_wrapper.h"

#include <algorithm>
#include <cstring>
#include <numeric>

static constexpr byte_t CSV{ COBS_TINYFRAME_SENTINEL_VALUE };

namespace {
cobs_ret_t cobs_encode_vec(byte_vec_t& v) {
  return cobs_encode_tinyframe(v.data(), v.size());
}
}  // namespace

TEST_CASE("Inplace encoding validation") {
  SUBCASE("Null buffer pointer") {
    REQUIRE(cobs_encode_tinyframe(nullptr, 123) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Invalid buf_len") {
    char buf;
    REQUIRE(cobs_encode_tinyframe(&buf, 0) == COBS_RET_ERR_BAD_ARG);
    REQUIRE(cobs_encode_tinyframe(&buf, 1) == COBS_RET_ERR_BAD_ARG);
  }

  SUBCASE("Invalid sentinel values") {
    byte_vec_t buf{ CSV - 1, CSV - 1 };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
    buf = byte_vec_t{ CSV, CSV - 1 };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
    buf = byte_vec_t{ CSV - 1, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Nonzero run longer than 255") {
    byte_vec_t buf{ CSV };
    buf.insert(std::end(buf), 256, 0x01);
    buf.push_back(CSV);
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }

  SUBCASE("Non-final run of 255 bytes") {
    byte_vec_t buf{ CSV, 0x00 };
    buf.insert(std::end(buf), 255, 1);
    buf.push_back(0x00);
    buf.push_back(CSV);
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_ERR_BAD_PAYLOAD);
  }
}

TEST_CASE("Inplace encoding") {
  SUBCASE("Empty") {
    byte_vec_t buf{ CSV, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x01, 0x00 });
  }

  SUBCASE("One nonzero byte") {
    byte_vec_t buf{ CSV, 0x01, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x02, 0x01, 0x00 });
  }

  SUBCASE("One zero byte") {
    byte_vec_t buf{ CSV, 0x00, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x01, 0x01, 0x00 });
  }

  SUBCASE("Sentinel value (0x5A) as payload data") {
    byte_vec_t buf{ CSV, CSV, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x02, CSV, 0x00 });
  }

  SUBCASE("Multiple zero bytes") {
    byte_vec_t buf{ CSV, 0x00, 0x00, 0x00, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x01, 0x01, 0x01, 0x01, 0x00 });
  }

  SUBCASE("Mixed nonzero and zero") {
    byte_vec_t buf{ CSV, 0x11, 0x00, 0x22, CSV };
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
    REQUIRE(buf == byte_vec_t{ 0x02, 0x11, 0x02, 0x22, 0x00 });
  }

  SUBCASE("Longest possible run of 254 bytes") {
    byte_vec_t buf{ CSV, 0x00 };
    buf.insert(std::end(buf), 254, 1);
    buf.push_back(CSV);
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);
  }

  SUBCASE("Safe payload, all zero bytes") {
    byte_vec_t buf(COBS_TINYFRAME_SAFE_BUFFER_SIZE);
    std::fill(std::begin(buf), std::end(buf), byte_t{ 0x00 });
    buf[0] = CSV;
    buf[buf.size() - 1] = CSV;
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);

    byte_vec_t expected(buf.size());
    std::fill(std::begin(expected), std::end(expected), byte_t{ 0x01 });
    expected[expected.size() - 1] = 0x00;
    REQUIRE(buf == expected);
  }

  SUBCASE("Safe payload, no zero bytes") {
    byte_vec_t buf(COBS_TINYFRAME_SAFE_BUFFER_SIZE);
    std::iota(std::begin(buf), std::end(buf), byte_t{ 0x00 });
    buf[0] = CSV;
    buf[buf.size() - 1] = CSV;
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);

    byte_vec_t expected(buf.size());
    std::iota(std::begin(expected), std::end(expected), byte_t{ 0x00 });
    expected[0] = 0xFF;
    expected[expected.size() - 1] = 0x00;
    REQUIRE(buf == expected);
  }

  SUBCASE("Unsafe payload with 254B jumps") {
    byte_vec_t buf{ CSV };
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0x00);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(0x00);
    buf.insert(std::end(buf), 254, 0x01);
    buf.push_back(CSV);
    REQUIRE(cobs_encode_vec(buf) == COBS_RET_SUCCESS);

    byte_vec_t expected{ 0xFF };
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0xFF);
    expected.insert(std::end(expected), 254, 0x01);
    expected.push_back(0x00);
    REQUIRE(buf == expected);
  }
}

TEST_CASE("Inplace encoding round-trip") {
  SUBCASE("Small payloads") {
    for (size_t len : { size_t{ 0 },
                        size_t{ 1 },
                        size_t{ 2 },
                        size_t{ 3 },
                        size_t{ 10 },
                        size_t{ 127 } }) {
      byte_vec_t buf(len + 2);
      buf[0] = CSV;
      buf[len + 1] = CSV;
      for (size_t i{ 1 }; i <= len; ++i) {
        buf[i] = byte_t(i);
      }
      byte_vec_t original(buf.begin() + 1, buf.begin() + 1 + static_cast<long>(len));

      REQUIRE(cobs_encode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
      REQUIRE(buf.back() == 0x00);
      REQUIRE(std::none_of(buf.data(), buf.data() + buf.size() - 1, [](byte_t b) {
        return b == 0;
      }));

      REQUIRE(cobs_decode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
      REQUIRE(buf[0] == CSV);
      REQUIRE(buf[len + 1] == CSV);
      REQUIRE(byte_vec_t(buf.begin() + 1, buf.begin() + 1 + static_cast<long>(len)) ==
              original);
    }
  }

  SUBCASE("Payload with zeros") {
    byte_vec_t buf{ CSV, 0x00, 0x11, 0x00, 0x22, 0x00, CSV };
    byte_vec_t original(buf.begin() + 1, buf.end() - 1);
    REQUIRE(cobs_encode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
    REQUIRE(cobs_decode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
    REQUIRE(byte_vec_t(buf.begin() + 1, buf.end() - 1) == original);
  }

  SUBCASE("Payload containing sentinel value") {
    byte_vec_t buf{ CSV, CSV, 0x11, CSV, CSV };
    byte_vec_t original(buf.begin() + 1, buf.end() - 1);
    REQUIRE(cobs_encode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
    REQUIRE(cobs_decode_tinyframe(buf.data(), buf.size()) == COBS_RET_SUCCESS);
    REQUIRE(byte_vec_t(buf.begin() + 1, buf.end() - 1) == original);
  }
}

namespace {
void verify_encode_inplace(byte_t* inplace, size_t payload_len) {
  byte_vec_t external(COBS_ENCODE_MAX(payload_len));
  size_t external_len;
  REQUIRE(cobs_encode(inplace + 1,
                      payload_len,
                      external.data(),
                      external.size(),
                      &external_len) == COBS_RET_SUCCESS);

  REQUIRE(cobs_encode_tinyframe(inplace, payload_len + 2) == COBS_RET_SUCCESS);
  REQUIRE(byte_vec_t(inplace, inplace + payload_len + 2) == external);
}

void fill_inplace(byte_t* inplace, size_t payload_len, byte_t f) {
  memset(inplace + 1, f, payload_len);
  inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
  inplace[payload_len + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
}
}  // namespace

TEST_CASE("Encode: Inplace == External") {
  byte_t inplace[COBS_TINYFRAME_SAFE_BUFFER_SIZE];

  SUBCASE("Fill with zeros") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0x00);
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Fill with nonzeros") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0x01);
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Fill with 0xFF") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, 0xFF);
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Fill with sentinel value") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      fill_inplace(inplace, i, CSV);
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Fill with zero/one pattern") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
      for (auto j{ 1u }; j < i; ++j) {
        inplace[j] = j & 1;
      }
      inplace[i + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Fill with one/zero pattern") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
      for (auto j{ 1u }; j < i; ++j) {
        inplace[j] = (j & 1) ^ 1;
      }
      inplace[i + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
      verify_encode_inplace(inplace, i);
    }
  }

  SUBCASE("Ascending bytes") {
    for (auto i{ 0u }; i < sizeof(inplace) - 2; ++i) {
      inplace[0] = COBS_TINYFRAME_SENTINEL_VALUE;
      for (auto j{ 1u }; j <= i; ++j) {
        inplace[j] = byte_t(j);
      }
      inplace[i + 1] = COBS_TINYFRAME_SENTINEL_VALUE;
      verify_encode_inplace(inplace, i);
    }
  }
}
