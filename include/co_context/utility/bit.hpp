#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

namespace co_context {

template<std::integral T>
consteval T bit_top() noexcept {
    return T(-1) ^ (T(-1) >> 1);
}

template<std::integral T>
inline constexpr T lowbit(T x) noexcept {
    return x & (-x);
}

template<std::integral T>
inline constexpr bool is_pow_of_two(T x) noexcept {
    return lowbit(x) == x;
}

template<typename T>
concept trival_ptr = std::is_trivially_copyable_v<T>
                     && sizeof(T) == sizeof(std::uintptr_t);

} // namespace co_context