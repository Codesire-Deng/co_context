#pragma once

#include <memory>

#ifdef __cpp_lib_assume_aligned
#define CO_CONTEXT_ASSUME_ALIGNED(align) std::assume_aligned<align>
#else
#define CO_CONTEXT_ASSUME_ALIGNED(...)
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#if defined(__i386__) || defined(__x86_64__)
#define CO_CONTEXT_PAUSE() __builtin_ia32_pause()
#elif defined(__riscv)
#define CO_CONTEXT_PAUSE() __asm__ volatile("fence iorw, iorw")
#endif
#elif defined(_MSC_VER)
#define CO_CONTEXT_PAUSE() _mm_pause()
#else
#define CO_CONTEXT_PAUSE() __builtin_ia32_pause()
#endif

#if (defined(__GNUC__) || defined(__GNUG__)) \
    && __has_include(<sys/single_threaded.h>)
#define CO_CONTEXT_IS_SINGLE_THREADED (__gnu_cxx::__is_single_threaded())
#else
// TODO find out ways to judge this
#define CO_CONTEXT_IS_SINGLE_THREADED false
#endif
