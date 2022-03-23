#pragma once

template<typename Lambda>
struct Defer : Lambda {
    ~Defer() { Lambda::operator()(); }
};

template<typename Lambda>
Defer(Lambda) -> Defer<Lambda>;