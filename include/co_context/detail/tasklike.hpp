#pragma once

namespace co_context {

template<typename T>
concept tasklike = requires {
    typename T::promise_type;
    typename T::value_type;
    typename T::is_tasklike;
};

} // namespace co_context
