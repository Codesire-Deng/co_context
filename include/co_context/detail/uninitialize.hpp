#pragma once

namespace co_context::detail {

template<typename T>
struct real_sized {
    T _;
};

template<typename T>
struct uninitialized_buffer {
    alignas(alignof(real_sized<T>)) char data[sizeof(real_sized<T>)];
};

template<>
struct uninitialized_buffer<void> {};

template<typename T>
struct uninitialize {
    using type = detail::uninitialized_buffer<T>;
};

template<>
struct uninitialize<void> {
    using type = void;
};

template<typename T>
using uninitialize_t = uninitialize<T>;

} // namespace co_context::detail
