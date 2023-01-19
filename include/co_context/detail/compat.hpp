#pragma once

#include <memory>

#ifdef __cpp_lib_assume_aligned
#define CO_CONTEXT_ASSUME_ALIGNED(align) std::assume_aligned<align>
#else
#define CO_CONTEXT_ASSUME_ALIGNED(...)
#endif

#if defined(__clang__) || defined(__GNUC__) || defined(__GNUG__)
#define CO_CONTEXT_PAUSE __builtin_ia32_pause
#elif defined(_MSC_VER)
#define CO_CONTEXT_PAUSE _mm_pause
#else
#define CO_CONTEXT_PAUSE __builtin_ia32_pause
#endif