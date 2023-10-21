#pragma once

#include <co_context/mpl/type_list.hpp>

#include <cstddef>
#include <tuple>
#include <type_traits>

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
concept Tuple = is_tuple<T>::value;

template<Tuple... in>
struct tuple_cat {
  private:
    using cat_type_list = concat_t<type_list_from_t<in>...>;

  public:
    using type = typename cat_type_list::template to<std::tuple>;
};

template<Tuple... in>
using tuple_cat_t = typename tuple_cat<in...>::type;

template<Tuple in, size_t... idx>
struct tuple_at {
    using type = std::tuple<std::tuple_element_t<idx, in>...>;
};

template<Tuple in, size_t... idx>
using tuple_at_t = typename tuple_at<in, idx...>::type;

template<Tuple in, typename E>
struct tuple_remove {
  private:
    using remove_type_list = remove_t<type_list_from_t<in>, E>;

  public:
    using type = typename remove_type_list::template to<std::tuple>;
};

template<Tuple in, typename E>
using tuple_remove_t = tuple_remove<in, E>::type;

} // namespace co_context::mpl
