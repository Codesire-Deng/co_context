#pragma once

#include <atomic>
#include <type_traits>

namespace co_context {

template<typename T>
    requires std::is_trivially_copyable_v<T>
inline std::atomic<T> &as_atomic(T &value) noexcept {
    return *reinterpret_cast<std::atomic<T> *>(std::addressof(value));
}

template<typename T>
    requires std::is_trivially_copyable_v<T>
inline const std::atomic<T> &as_atomic(const T &value) noexcept {
    return *reinterpret_cast<const std::atomic<T> *>(std::addressof(value));
}

} // namespace co_context