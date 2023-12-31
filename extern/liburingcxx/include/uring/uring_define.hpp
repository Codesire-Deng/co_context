#pragma once

#include <cstdint>

namespace liburingcxx {

enum uring_setup : uint64_t {
    // from (1ULL << 32) to (1ULL << 63)
    sqe_reorder = 1ULL << 32
};

} // namespace liburingcxx
