#pragma once

#include "co_context.hpp"

namespace co_context {

namespace eager {
    constexpr auto nop() noexcept { return std::suspend_never{}; }
} // namespace eager

} // namespace co_context
