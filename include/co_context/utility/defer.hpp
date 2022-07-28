#pragma once

namespace co_context {

template<typename Lambda>
struct defer : Lambda {
    ~defer() { Lambda::operator()(); }
};

template<typename Lambda>
defer(Lambda) -> defer<Lambda>;

} // namespace co_context
