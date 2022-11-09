#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace co_context {

template<typename From, typename To>
concept reinterpretable_to =
    sizeof(From) == sizeof(To) && std::is_fundamental_v<From>
    && (!std::is_void_v<From>) && std::is_fundamental_v<To>
    && (!std::is_void_v<To>);

template<reinterpretable_to<uintptr_t> T>
inline std::uintptr_t &as_uintptr(T &value) noexcept {
    return *reinterpret_cast<uintptr_t *>(std::addressof(value));
}

template<reinterpretable_to<uintptr_t> T>
inline const std::uintptr_t &as_uintptr(const T &value) noexcept {
    return *reinterpret_cast<const uintptr_t *>(std::addressof(value));
}

} // namespace co_context
