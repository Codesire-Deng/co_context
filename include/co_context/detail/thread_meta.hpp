#pragma once

#include <co_context/config/io_context.hpp>
#include <co_context/detail/uring_type.hpp>

namespace co_context {

class io_context;

} // namespace co_context

namespace co_context::detail {

struct worker_meta;

struct alignas(config::cache_line_size) thread_meta {
    // The running io_context on this thread.
    io_context *ctx = nullptr;
    worker_meta *worker = nullptr; // ctx + offset = worker

    config::ctx_id_t ctx_id = static_cast<config::ctx_id_t>(-1);
};

extern thread_local thread_meta this_thread; // NOLINT(*global-variables)

} // namespace co_context::detail
