#pragma once

// #include <concepts>
#include <type_traits>
#include <span>

namespace co_context {

template<typename T>
    requires std::is_standard_layout_v<T>
inline constexpr auto as_buf(T *ptr) {
    return std::span<char, sizeof(T)>{reinterpret_cast<char *>(ptr), sizeof(T)};
}

template<typename T>
    requires std::is_standard_layout_v<T>
inline constexpr auto as_buf(const T *ptr) {
    return std::span<const char, sizeof(T)>{
        reinterpret_cast<const char *>(ptr), sizeof(T)};
}

} // namespace co_context
