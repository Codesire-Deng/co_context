#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

namespace co_context {

template<std::unsigned_integral T>
inline constexpr T bit_top = T(-1) ^ (T(-1) >> 1);

template<std::integral T>
inline constexpr T lowbit(T x) noexcept {
    return x & (-x);
}

} // namespace co_context
