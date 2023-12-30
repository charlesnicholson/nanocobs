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
    byte_vec_t src(LEN), dec(LEN), enc(COBS_ENCODE_MAX(LEN));

    while (--s_iterations > 0) {
      std::generate(src.begin(), src.end(), [&]() { return byte_t(mt()); });
      memset(src.data() + 1000, 0xAA, 256 * 10);  // nonzero run

      size_t enc_len{ 0u };
      REQUIRE(cobs_encode(src.data(), src.size(), enc.data(), enc.size(), &enc_len) ==
              COBS_RET_SUCCESS);

      REQUIRE(enc_len >= LEN);
      REQUIRE(enc_len <= COBS_ENCODE_MAX(LEN));
      REQUIRE(
          std::none_of(enc.data(), enc.data() + enc_len - 1, [](byte_t b) { return !b; }));
      REQUIRE(enc[enc_len - 1] == 0);

      size_t dec_len{ 0u };
      REQUIRE(cobs_decode(enc.data(), enc_len, dec.data(), dec.size(), &dec_len) ==
              COBS_RET_SUCCESS);

      REQUIRE(dec_len == LEN);
      REQUIRE(src == dec);
    }
  } };

  std::vector<std::thread> threads;
  for (auto i{ 0u }, n{ std::max(1u, std::thread::hardware_concurrency() - 1) }; i < n;
       ++i) {
    threads.emplace_back(thread_proc, i);
  }

  for (auto& t : threads) {
    t.join();
  }
}
