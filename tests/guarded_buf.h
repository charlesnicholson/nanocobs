#pragma once

#include "byte_vec.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#define COBS_TEST_HAVE_GUARD_PAGES 1
#endif

namespace cobs_test {

// The last usable byte abuts an inaccessible page, so one byte of overrun faults at
// the offending instruction. |ofs| shifts the start away from natural alignment.
class guarded_buf {
 public:
  explicit guarded_buf(size_t len, size_t ofs = 0) : len_{ len } {
#if defined(COBS_TEST_HAVE_GUARD_PAGES)
    size_t const page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    size_t const need = len + ofs;
    size_t const body = ((need + page - 1) / page) * page;
    map_len_ = body + page;
    void* const m =
        ::mmap(nullptr, map_len_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) {
      throw std::bad_alloc{};
    }
    base_ = static_cast<unsigned char*>(m);
    if (::mprotect(base_ + body, page, PROT_NONE) != 0) {
      ::munmap(base_, map_len_);
      throw std::bad_alloc{};
    }
    // End the usable region flush against the guard page, then back off by |ofs|
    // so that misalignment is preserved without giving up the overrun trap.
    data_ = base_ + body - len - ofs;
#elif defined(_WIN32)
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    size_t const page = si.dwPageSize;
    size_t const need = len + ofs;
    size_t const body = ((need + page - 1) / page) * page;
    map_len_ = body + page;
    base_ = static_cast<unsigned char*>(
        ::VirtualAlloc(nullptr, map_len_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!base_) {
      throw std::bad_alloc{};
    }
    DWORD old = 0;
    if (!::VirtualProtect(base_ + body, page, PAGE_NOACCESS, &old)) {
      ::VirtualFree(base_, 0, MEM_RELEASE);
      throw std::bad_alloc{};
    }
    data_ = base_ + body - len - ofs;
#else
    // No guard pages available: fall back to a canary suffix, checked on destroy.
    base_ = new unsigned char[len + ofs + kCanary];
    data_ = base_ + ofs;
    for (size_t i = 0; i < kCanary; ++i) {
      data_[len + i] = 0xCD;
    }
#endif
  }

  guarded_buf(guarded_buf const&) = delete;
  guarded_buf& operator=(guarded_buf const&) = delete;

  ~guarded_buf() {
#if defined(COBS_TEST_HAVE_GUARD_PAGES)
    ::munmap(base_, map_len_);
#elif defined(_WIN32)
    ::VirtualFree(base_, 0, MEM_RELEASE);
#else
    for (size_t i = 0; i < kCanary; ++i) {
      if (data_[len_ + i] != 0xCD) {
        std::abort();
      }
    }
    delete[] base_;
#endif
  }

  byte_t* data() {
    return data_;
  }
  byte_t const* data() const {
    return data_;
  }
  size_t size() const {
    return len_;
  }

  void fill(byte_t v) {
    for (size_t i = 0; i < len_; ++i) {
      data_[i] = v;
    }
  }

  void assign(byte_t const* src, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      data_[i] = src[i];
    }
  }

 private:
  static size_t constexpr kCanary = 32;
  unsigned char* base_ = nullptr;
  unsigned char* data_ = nullptr;
  size_t len_ = 0;
  size_t map_len_ = 0;
};

}  // namespace cobs_test
