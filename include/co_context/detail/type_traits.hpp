#pragma once

#include <type_traits> // IWYU pragma: export

namespace co_context::detail {

template<typename...>
inline constexpr bool false_v = false;

template<typename T>
struct remove_rvalue_reference {
    using type = T;
};

template<typename T>
struct remove_rvalue_reference<T &&> {
    using type = T;
};

template<typename T>
using remove_rvalue_reference_t = typename remove_rvalue_reference<T>::type;

template<typename Awaiter>
using get_awaiter_result_t = decltype(std::declval<Awaiter>().await_resume());

} // namespace co_context::detail
