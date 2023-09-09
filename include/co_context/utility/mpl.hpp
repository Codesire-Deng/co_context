#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace co_context::mpl::detail {

template<size_t lhs, size_t rhs>
struct is_less {
    static constexpr bool value = lhs < rhs;
};

template<size_t lhs, size_t rhs>
inline constexpr bool is_less_v = is_less<lhs, rhs>::value;

} // namespace co_context::mpl::detail

namespace co_context::mpl {

template<typename... Ts>
struct type_list {
    struct is_type_list {};

    using type = type_list;

    static constexpr size_t size = sizeof...(Ts);

    template<typename... Us>
    using append = type_list<Ts..., Us...>;
    template<typename... Us>
    using prepend = type_list<Us..., Ts...>;

    template<template<typename...> typename T>
    using to = T<Ts...>;
};

template<typename... Ts>
struct type_list_from {};

template<typename... Ts>
struct type_list_from<std::tuple<Ts...>> : type_list<Ts...> {};

template<typename Type_list>
concept TL = requires {
    typename Type_list::is_type_list;
    typename Type_list::type;
};

static_assert(TL<type_list<>>);

template<TL in, template<typename> class F>
struct map;

template<template<typename> class F, typename... Ts>
struct map<type_list<Ts...>, F> : type_list<typename F<Ts>::type...> {};

template<TL in, template<typename> class F>
using map_t = typename map<in, F>::type;

template<TL in, template<typename> class P, TL out = type_list<>>
struct filter : out {};

template<template<typename> class P, TL out, typename H, typename... Ts>
struct filter<type_list<H, Ts...>, P, out>
    : std::conditional_t<
          P<H>::value,
          filter<type_list<Ts...>, P, typename out::template append<H>>,
          filter<type_list<Ts...>, P, out>> {};

template<typename T>
struct id {
    using type = T;
};

template<TL in, typename init, template<typename, typename> class op>
struct fold : id<init> {};

template<
    typename acc,
    template<typename, typename>
    class op,
    typename H,
    typename... Ts>
struct fold<type_list<H, Ts...>, acc, op>
    : fold<type_list<Ts...>, typename op<acc, H>::type, op> {};

template<TL in, typename init, template<typename, typename> class op>
using fold_t = typename fold<in, init, op>::type;

template<TL... in>
struct concat;

template<>
struct concat<> : type_list<> {};

template<TL in>
struct concat<in> : in {};

template<TL... in>
using concat_t = typename concat<in...>::type;

template<TL in1, TL in2, TL... Rest>
struct concat<in1, in2, Rest...> : concat_t<concat_t<in1, in2>, Rest...> {};

template<typename... Ts1, typename... Ts2>
struct concat<type_list<Ts1...>, type_list<Ts2...>>
    : type_list<Ts1..., Ts2...> {};

static_assert(std::is_same_v<concat_t<type_list<>>, type_list<>>);

template<TL in, typename E>
struct contain : std::false_type {};

template<typename E, typename... Ts>
struct contain<type_list<Ts...>, E>
    : std::bool_constant<(false || ... || std::is_same_v<E, Ts>)> {};

template<TL in, typename E>
constexpr bool contain_v = contain<in, E>::value;

template<TL in>
class unique {
    template<TL acc, typename E>
    using add = std::
        conditional_t<contain_v<acc, E>, acc, typename acc::template append<E>>;

  public:
    using type = fold_t<in, type_list<>, add>;
};

template<TL in>
using unique_t = typename unique<in>::type;

template<TL in>
struct reverse;

template<>
struct reverse<type_list<>> : type_list<> {};

template<typename T, typename... Ts>
struct reverse<type_list<T, Ts...>>
    : reverse<type_list<Ts...>>::template append<T> {};

template<TL in>
using reverse_t = typename reverse<in>::type;

template<TL in, typename E>
struct count {};

template<TL in, typename E>
constexpr auto count_v = count<in, E>::value;

template<typename E, typename T>
struct count<type_list<T>, E>
    : std::integral_constant<size_t, (unsigned(std::is_same_v<E, T>))> {};

template<typename E, typename... Ts>
struct count<type_list<Ts...>, E>
    : std::integral_constant<
          size_t,
          (0 + ... + unsigned(std::is_same_v<E, Ts>))> {};

template<TL in, size_t idx>
    requires(idx < in::size)
struct select;

template<size_t idx, typename... Ts>
using select_t = typename select<type_list<Ts...>, idx>::type;

template<typename H, typename... Ts>
struct select<type_list<H, Ts...>, 0> : id<H> {};

template<size_t idx, typename H, typename... Ts>
struct select<type_list<H, Ts...>, idx> : select<type_list<Ts...>, idx - 1> {};

template<TL in, size_t N, class = std::make_index_sequence<in::size>>
struct first_N;

template<std::size_t N, class... Ts, std::size_t... Is>
struct first_N<type_list<Ts...>, N, std::index_sequence<Is...>>
    : concat_t<std::conditional_t<
          detail::is_less_v<Is, N>,
          type_list<Ts>,
          type_list<>>...> {};

template<TL in, size_t N>
using first_N_t = typename first_N<in, N>::type;

static_assert(std::is_same_v<first_N_t<type_list<int>, 1>, type_list<int>>);

} // namespace co_context::mpl

namespace co_context::mpl {

template<typename T>
struct is_tuple : std::false_type {};

template<>
struct is_tuple<std::tuple<>> : std::true_type {};

template<typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template<typename T>
constexpr bool is_tuple_v = is_tuple<T>::value;

template<typename T>
concept tuple = is_tuple<T>::value;

template<tuple... Tuples>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Tuples>()...));

namespace detail {

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

} // namespace detail

template<tuple Tuple, size_t... idx>
using select_tuple_t = tuple_cat_t<std::tuple_element_t<idx, Tuple>...>;

template<size_t N, typename T, typename... Ts>
constexpr size_t count_first_N = count<first_N<type_list<Ts...>, N>, T>::value;

template<typename T, typename... From>
using remove_t = tuple_cat_t<std::conditional_t<
    std::is_same_v<T, From>,
    std::tuple<>,
    std::tuple<From>>...>;

template<typename T>
using uninitialized = std::
    conditional_t<std::is_void_v<T>, void, detail::uninitialized_buffer<T>>;

} // namespace co_context::mpl

namespace co_context::detail {

template<typename T>
using is_not_void = std::bool_constant<!std::is_void_v<T>>;

template<typename... Ts>
using clear_void_t =
    typename mpl::filter<mpl::type_list<Ts...>, is_not_void>::type;

template<typename F, int begin, int... idx>
constexpr void static_for_impl(
    F &&f, std::integer_sequence<int, idx...> /*unused*/
) {
    static_assert(sizeof...(idx) > 0);
    (..., std::forward<F>(f)(std::integral_constant<int, begin + idx>{}));
}

template<typename F, int begin, int... idx>
constexpr void static_rfor_impl(
    F &&f, std::integer_sequence<int, idx...> /*unused*/
) {
    static_assert(sizeof...(idx) > 0);
    (..., std::forward<F>(f)(
              std::integral_constant<int, begin + (sizeof...(idx) - idx - 1)>{}
          ));
}

} // namespace co_context::detail

namespace co_context::mpl {

template<typename T>
struct reverse_sequence;

template<typename T>
struct reverse_sequence<std::integer_sequence<T>> {
    template<T... app>
    using append = std::integer_sequence<T, app...>;
    using type = std::integer_sequence<T>;
};

template<typename T, T idx, T... idxs>
struct reverse_sequence<std::integer_sequence<T, idx, idxs...>> {
    template<T... app>
    using append = reverse_sequence<
        std::integer_sequence<T, idxs...>>::template append<idx, app...>;
    using type = reverse_sequence<
        std::integer_sequence<T, idxs...>>::template append<idx>;
};

template<typename T>
using reverse_sequence_t = reverse_sequence<T>::type;

static_assert(std::is_same_v<
              reverse_sequence_t<std::integer_sequence<int, 0, 10, 3, 7>>,
              std::integer_sequence<int, 7, 3, 10, 0>>);

template<int begin, int end, typename F>
constexpr void static_for(F &&f) {
    if constexpr (begin < end) {
        co_context::detail::static_for_impl<F, begin>(
            std::forward<F>(f), std::make_integer_sequence<int, end - begin>()
        );
    }
}

template<int begin, int end, typename F>
constexpr void static_rfor(F &&f) {
    if constexpr (begin < end) {
        co_context::detail::static_rfor_impl<F, begin>(
            std::forward<F>(f), std::make_integer_sequence<int, end - begin>()
        );
    }
}

} // namespace co_context::mpl
