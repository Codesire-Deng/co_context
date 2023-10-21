#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace co_context::mpl::detail {

template<size_t lhs, size_t rhs>
struct is_less {
    static constexpr bool value = lhs < rhs;
};

template<size_t lhs, size_t rhs>
inline constexpr bool is_less_v = is_less<lhs, rhs>::value;

inline constexpr size_t npos = -1ULL;

template<template<typename...> class P>
struct negate {
    template<typename... Ts>
    using type = std::bool_constant<!P<Ts...>::value>;
};

template<typename T, typename U>
struct is_not_same : std::bool_constant<!std::is_same_v<T, U>> {};

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

template<typename>
struct type_list_from {};

template<template<typename...> typename T, typename... Ts>
struct type_list_from<T<Ts...>> : type_list<Ts...> {};

template<typename T>
using type_list_from_t = typename type_list_from<T>::type;

template<typename Type_list>
concept TL = requires {
    typename Type_list::is_type_list;
    typename Type_list::type;
};

static_assert(TL<type_list<>>);

template<TL in>
    requires(in::size > 0)
struct head;

template<typename H, typename... Ts>
struct head<type_list<H, Ts...>> {
    using type = H;
};

template<TL in>
using head_t = typename head<in>::type;

template<TL in>
    requires(in::size > 0)
struct tails;

template<typename H, typename... Ts>
struct tails<type_list<H, Ts...>> {
    using type = type_list<Ts...>;
};

template<TL in>
using tails_t = typename tails<in>::type;

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

template<TL in, template<typename> class P>
using filter_t = typename filter<in, P>::type;

template<TL in, typename E>
struct remove {
  private:
    template<typename T>
    using is_not_E = typename detail::is_not_same<E, T>;

  public:
    using type = filter_t<in, is_not_E>;
};

template<TL in, typename E>
using remove_t = typename remove<in, E>::type;

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

template<TL in>
struct concat<in> : in {};

template<typename... Ts1, typename... Ts2>
struct concat<type_list<Ts1...>, type_list<Ts2...>>
    : type_list<Ts1..., Ts2...> {};

template<TL in1, TL in2, TL... Rest>
struct concat<in1, in2, Rest...>
    : concat<typename concat<in1, in2>::type, Rest...> {};

template<TL... in>
using concat_t = typename concat<in...>::type;

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

template<typename E, typename... Ts>
struct count<type_list<Ts...>, E>
    : std::integral_constant<
          size_t,
          (0 + ... + unsigned(std::is_same_v<E, Ts>))> {};

template<TL in, typename E>
constexpr size_t count_v = count<in, E>::value;

template<TL in, size_t idx>
    requires(idx < in::size)
struct select;

template<size_t idx, typename... Ts>
using select_t = typename select<type_list<Ts...>, idx>::type;

template<typename H, typename... Ts>
struct select<type_list<H, Ts...>, 0> : id<H> {};

template<size_t idx, typename H, typename... Ts>
struct select<type_list<H, Ts...>, idx> : select<type_list<Ts...>, idx - 1> {};

template<TL in, size_t n, class = std::make_index_sequence<in::size>>
    requires(n <= in::size)
struct first_n;

template<std::size_t n, class... Ts, std::size_t... Is>
struct first_n<type_list<Ts...>, n, std::index_sequence<Is...>>
    : concat_t<std::conditional_t<
          detail::is_less_v<Is, n>,
          type_list<Ts>,
          type_list<>>...> {};

template<TL in, size_t n>
using first_n_t = typename first_n<in, n>::type;

static_assert(std::is_same_v<first_n_t<type_list<int>, 1>, type_list<int>>);

} // namespace co_context::mpl

namespace co_context::mpl::detail {

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

} // namespace co_context::mpl::detail
