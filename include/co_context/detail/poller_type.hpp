#pragma once

#ifdef CO_CONTEXT_USE_IO_URING
#include "co_context/detail/uring_type.hpp"

namespace co_context::detail {
    using poller_type = detail::uring;
} // namespace co_context::detail
#endif

#ifdef CO_CONTEXT_USE_EPOLL
#include "co_context/detail/epoll.hpp"

namespace co_context::detail {
    using poller_type = epoll;
} // namespace co_context::detail
#endif
