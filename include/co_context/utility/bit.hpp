#pragma once

#include <concepts>

namespace co_context {

template<std::integral T>
consteval T bit_top() noexcept {
    return T(-1) ^ (T(-1) >> 1);
}

}