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

template<TL in>
    requires(in::size > 0)
struct last;

template<typename H>
struct last<type_list<H>> {
    using type = H;
};

template<typename H, typename H2, typename... Ts>
struct last<type_list<H, H2, Ts...>> : last<type_list<H2, Ts...>> {};

template<TL in>
using last_t = typename last<in>::type;

template<TL in, typename out = type_list<>>
struct drop_last;

template<typename L, typename out>
struct drop_last<type_list<L>, out> {
    using type = out;
};

template<typename H, typename H2, typename... Ts, typename out>
struct drop_last<type_list<H, H2, Ts...>, out>
    : drop_last<type_list<H2, Ts...>, typename out::template append<H>> {};

template<TL in>
using drop_last_t = typename drop_last<in>::type;

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

template<TL in, template<typename> typename P>
struct remove_if {
  private:
    template<typename E>
    using not_P = typename detail::negate<P>::template type<E>;

  public:
    using type = filter_t<in, not_P>;
};

template<TL in, template<typename> typename P>
using remove_if_t = typename remove_if<in, P>::type;

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

template<TL in, typename E>
struct contain : std::false_type {};

template<typename E, typename... Ts>
struct contain<type_list<Ts...>, E>
    : std::bool_constant<(false || ... || std::is_same_v<E, Ts>)> {};

template<TL in, typename E>
constexpr bool contain_v = contain<in, E>::value;

inline constexpr size_t npos = -1ULL;

namespace detail {
    template<TL in, typename E, size_t offset>
    struct find_impl;

    template<typename E, size_t offset>
    struct find_impl<type_list<>, E, offset>
        : std::integral_constant<size_t, npos> {};

    template<typename E, typename H, typename... Ts, size_t offset>
    struct find_impl<type_list<H, Ts...>, E, offset>
        : std::conditional_t<
              std::is_same_v<E, H>,
              std::integral_constant<size_t, offset>,
              find_impl<type_list<Ts...>, E, offset + 1>> {};

    template<TL in, template<typename> typename P, size_t offset>
    struct find_if_impl;

    template<template<typename> typename P, size_t offset>
    struct find_if_impl<type_list<>, P, offset>
        : std::integral_constant<size_t, npos> {};

    template<
        template<typename>
        typename P,
        typename H,
        typename... Ts,
        size_t offset>
    struct find_if_impl<type_list<H, Ts...>, P, offset>
        : std::conditional_t<
              P<H>::value,
              std::integral_constant<size_t, offset>,
              find_if_impl<type_list<Ts...>, P, offset + 1>> {};
} // namespace detail

template<TL in, typename E>
struct find : detail::find_impl<in, E, 0> {};

template<TL in, typename E>
using find_t = typename find<in, E>::type;

template<TL in, typename E>
constexpr size_t find_v = find<in, E>::value;

template<typename in, template<typename> typename P>
struct find_if : detail::find_if_impl<in, P, 0> {};

template<typename in, template<typename> typename P>
using find_if_t = typename find_if<in, P>::type;

template<TL in, template<typename> typename P>
constexpr size_t find_if_v = find_if<in, P>::value;

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
struct at;

template<typename H, typename... Ts>
struct at<type_list<H, Ts...>, 0> : id<H> {};

template<size_t idx, typename H, typename... Ts>
struct at<type_list<H, Ts...>, idx> : at<type_list<Ts...>, idx - 1> {};

template<TL in, size_t idx>
using at_t = typename at<in, idx>::type;

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

template<TL in, typename Old, typename New>
struct replace {
  private:
    template<typename T>
    using substitute = std::conditional<std::is_same_v<T, Old>, New, T>;

  public:
    using type = map_t<in, substitute>;
};

template<typename in, typename Old, typename New>
using replace_t = typename replace<in, Old, New>::type;

template<TL in1, TL in2>
struct union_set {
    using type = unique_t<concat_t<in1, in2>>;
};

template<TL in1, TL in2>
using union_set_t = typename union_set<in1, in2>::type;

template<TL in1, TL in2>
class intersection_set {
    template<typename E>
    using in1_contain = contain<in1, E>;

  public:
    using type = filter_t<in2, in1_contain>;
};

template<TL in1, TL in2>
using intersection_set_t = typename intersection_set<in1, in2>::type;

template<TL in1, TL in2>
class difference_set {
    template<typename E>
    struct in2_contain : contain<in2, E> {};

  public:
    using type = remove_if_t<in1, in2_contain>;
};

template<TL in1, TL in2>
using difference_set_t = typename difference_set<in1, in2>::type;

template<TL in1, TL in2>
struct symmetric_difference_set {
    using type =
        concat_t<difference_set_t<in1, in2>, difference_set_t<in2, in1>>;
};

template<TL in1, TL in2>
using symmetric_difference_set_t =
    typename symmetric_difference_set<in1, in2>::type;

template<
    TL in,
    template<typename>
    typename P,
    typename out = type_list<type_list<>>>
struct split_if;

template<template<typename> typename P, typename out>
struct split_if<type_list<>, P, out> {
  private:
    using no_empty_t = remove_t<out, type_list<>>;
    using reverse_outer_t = reverse_t<no_empty_t>;

  public:
    using type = reverse_outer_t;
};

template<
    template<typename>
    typename P,
    typename H,
    typename... Ts,
    typename head_list,
    typename... lists>
struct split_if<type_list<H, Ts...>, P, type_list<head_list, lists...>>
    : std::conditional_t<
          P<H>::value,
          split_if<
              type_list<Ts...>,
              P,
              type_list<type_list<>, head_list, lists...>>,
          split_if<
              type_list<Ts...>,
              P,
              type_list<typename head_list::template append<H>, lists...>>> {};

template<TL in, template<typename> typename P>
using split_if_t = typename split_if<in, P>::type;

template<TL in, typename delimiter>
struct split {
  private:
    template<typename E>
    struct is_delimiter : std::is_same<delimiter, E> {};

  public:
    using type = split_if_t<in, is_delimiter>;
};

template<TL in, typename delimiter>
using split_t = typename split<in, delimiter>::type;

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
    using append = typename reverse_sequence<
        std::integer_sequence<T, idxs...>>::template append<idx, app...>;
    using type = typename reverse_sequence<
        std::integer_sequence<T, idxs...>>::template append<idx>;
};

template<typename T>
using reverse_sequence_t = typename reverse_sequence<T>::type;

static_assert(std::is_same_v<
              reverse_sequence_t<std::integer_sequence<int, 0, 10, 3, 7>>,
              std::integer_sequence<int, 7, 3, 10, 0>>);

} // namespace co_context::mpl::detail

namespace std {

template<typename... Ts>
struct tuple_size<co_context::mpl::type_list<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template<std::size_t I, typename... Ts>
struct tuple_element<I, co_context::mpl::type_list<Ts...>> {
    using type = co_context::mpl::at_t<co_context::mpl::type_list<Ts...>, I>;
};

}
