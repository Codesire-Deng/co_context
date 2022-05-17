#pragma once

#include "co_context/config.hpp"

namespace co_context {

namespace eager {
    using io_state_t = config::eager_io_state_t;

    inline constexpr io_state_t io_ready = 1U;
    inline constexpr io_state_t io_wait = 1U << 1;
    inline constexpr io_state_t io_detached = 1U << 2;

}

} // namespace co_context