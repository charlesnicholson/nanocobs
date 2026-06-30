// doctest_wrapper.h - include doctest with warning suppression
#pragma once

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-W#warnings"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
#endif

// MSVC 19.51+ (VS 2026) rejects doctest's forward declarations of std types
// (warning C5285: specializing std templates is forbidden by N5014). Have
// doctest include the real std headers instead of forward-declaring them.
#ifdef _MSC_VER
#ifndef DOCTEST_CONFIG_USE_STD_HEADERS
#define DOCTEST_CONFIG_USE_STD_HEADERS
#endif
#endif

#include "doctest.h"

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC diagnostic pop
#endif
#endif

#ifdef __clang__
#pragma clang diagnostic pop
#endif
