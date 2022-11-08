#pragma once

#include <memory>

#ifdef __cpp_lib_assume_aligned
#define CO_CONTEXT_ASSUME_ALIGNED(align) std::assume_aligned<align>
#else
#define CO_CONTEXT_ASSUME_ALIGNED(...)
#endif