#pragma once

#include "co_context/config.hpp"

namespace co_context {

class io_context;

namespace detail {

    class worker_meta;

    struct alignas(config::cache_line_size) thread_meta {
        io_context *ctx;
        worker_meta *worker;
        config::tid_t tid;
    };

    extern thread_local thread_meta this_thread;

} // namespace detail

} // namespace co_context
