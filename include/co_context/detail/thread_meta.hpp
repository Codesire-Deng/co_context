#pragma once

#include "co_context/config.hpp"
#include "co_context/detail/uring_type.hpp"

namespace co_context {

class io_context;

} // namespace co_context

namespace co_context::detail {

struct worker_meta;

struct alignas(config::cache_line_size) thread_meta {
    io_context *ctx;
    uring *ring;
    worker_meta *worker;
    config::tid_t tid;
};

extern thread_local thread_meta this_thread;

} // namespace co_context::detail
