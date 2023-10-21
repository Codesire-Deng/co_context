#pragma once

#include <functional>
#include <utility>

namespace co_context::mpl::detail {

template<typename F, int begin, int... idx>
constexpr void static_for_impl(
    F &&f, std::integer_sequence<int, idx...> /*unused*/
) {
    static_assert(sizeof...(idx) > 0);
    (...,
     std::invoke(std::forward<F>(f), std::integral_constant<int, begin + idx>{})
    );
}

template<typename F, int begin, int... idx>
constexpr void static_rfor_impl(
    F &&f, std::integer_sequence<int, idx...> /*unused*/
) {
    static_assert(sizeof...(idx) > 0);
    (..., std::invoke(
              std::forward<F>(f),
              std::integral_constant<int, begin + (sizeof...(idx) - idx - 1)>{}
          ));
}

} // namespace co_context::mpl::detail

namespace co_context::mpl {

template<int begin, int end, typename F>
constexpr void static_for(F &&f) {
    if constexpr (begin < end) {
        co_context::mpl::detail::static_for_impl<F, begin>(
            std::forward<F>(f), std::make_integer_sequence<int, end - begin>()
        );
    }
}

template<int begin, int end, typename F>
constexpr void static_rfor(F &&f) {
    if constexpr (begin < end) {
        co_context::mpl::detail::static_rfor_impl<F, begin>(
            std::forward<F>(f), std::make_integer_sequence<int, end - begin>()
        );
    }
}

} // namespace co_context::mpl
