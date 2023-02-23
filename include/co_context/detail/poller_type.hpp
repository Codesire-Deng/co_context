#pragma once

#ifdef USE_IO_URING
#include "co_context/detail/uring_type.hpp"
#endif

#ifdef USE_EPOLL
#include "co_context/detail/epoll.hpp"
#endif
