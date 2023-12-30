#include "../cobs.h"
#include "byte_vec.h"

#include "doctest.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <random>
#include <thread>

auto constexpr LEN{ 4u * 1024u * 1024u };

namespace {
std::atomic<int> s_iterations{ 250u };
}

TEST_CASE("many random payloads") {
  auto const thread_proc{ [](unsigned seed) {
    std::mt19937 mt{ seed };  // deterministic
    byte_vec_t source(LEN), decoded(LEN), encoded(COBS_ENCODE_MAX(LEN));

    while (s_iterations > 0) {
      std::generate(source.begin(), source.end(), [&]() { return byte_t(mt() & 0xFF); });
      memset(source.data() + 1000, 0xAA, 256 * 10);  // nonzero run

      size_t enc_len{ 0u };
      REQUIRE(cobs_encode(source.data(),
                          source.size(),
                          encoded.data(),
                          encoded.size(),
                          &enc_len) == COBS_RET_SUCCESS);

      REQUIRE(enc_len >= LEN);
      REQUIRE(enc_len <= COBS_ENCODE_MAX(LEN));
      REQUIRE(std::none_of(encoded.data(),
                           encoded.data() + enc_len - 1,
                           [](byte_t const b) { return b == 0; }));
      REQUIRE(encoded[enc_len - 1] == 0);

      size_t dec_len{ 0u };
      REQUIRE(
          cobs_decode(encoded.data(), enc_len, decoded.data(), decoded.size(), &dec_len) ==
          COBS_RET_SUCCESS);

      REQUIRE(dec_len == LEN);
      REQUIRE(source == decoded);
      --s_iterations;
    }
  } };

  std::vector<std::thread> threads;
  auto const thread_count{ std::max(1u, std::thread::hardware_concurrency() - 1) };

  for (auto i{ 0u }; i < thread_count; ++i) {
    threads.emplace_back(thread_proc, i);
  }

  for (auto& t : threads) {
    t.join();
  }
}
