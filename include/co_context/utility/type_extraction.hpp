#pragma once

#include <tuple>
#include <type_traits>
#include <utility>

namespace co_context::mpl {

template<typename... Tuples>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Tuples>()...));

template<typename T, typename... From>
using remove_t = tuple_cat_t<std::conditional_t<
    std::is_same_v<T, From>,
    std::tuple<>,
    std::tuple<From>>...>;

} // namespace co_context::mpl