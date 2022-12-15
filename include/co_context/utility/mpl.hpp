#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace co_context::mpl {

template<typename T, typename... Ts>
constexpr size_t count = (0 + ... + unsigned(std::is_same_v<T, Ts>));

template<typename... Tuples>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Tuples>()...));

template<size_t idx, typename... Ts>
using select = std::tuple_element_t<idx, std::tuple<Ts...>>;

namespace detail {

    template<typename Tuple, size_t... idx>
    auto select_tuple_f(
        [[maybe_unused]] Tuple t, [[maybe_unused]] std::index_sequence<idx...> i
    ) {
        using type = tuple_cat_t<std::tuple_element_t<idx, Tuple>...>;
        return std::declval<type>();
    };

    template<typename T, typename... Ts>
    consteval size_t count_tuple_f([[maybe_unused]] std::tuple<Ts...> t) {
        return count<T, Ts...>;
    };

    template<size_t N, typename... Ts>
    using first_N_tuple_t = decltype(select_tuple_f(
        std::declval<std::tuple<Ts...>>(),
        std::declval<std::make_index_sequence<N>>()
    ));

} // namespace detail

template<typename Tuple, size_t... idx>
using select_tuple_t = tuple_cat_t<std::tuple_element_t<idx, Tuple>...>;

template<size_t N, typename T, typename... Ts>
constexpr size_t count_first_N =
    detail::count_tuple_f<T, detail::first_N_tuple_t<N, Ts...>>();

template<typename T, typename... From>
using remove_t = tuple_cat_t<std::conditional_t<
    std::is_same_v<T, From>,
    std::tuple<>,
    std::tuple<From>>...>;

template<typename T>
    requires(!std::is_void_v<T>)
union uninitialized_buffer {
    T value;
};

template<typename T>
using uninitialized =
    std::conditional_t<std::is_void_v<T>, void, uninitialized_buffer<T>>;

} // namespace co_context::mpl