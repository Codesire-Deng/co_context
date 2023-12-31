#pragma once

#include <co_context/config/io_context.hpp>

#include <condition_variable>
#include <mutex>

// Friend classes of io_context:
namespace co_context {

class mutex;
class condition_variable;
class counting_semaphore;

namespace detail {
    class lazy_resume_on;
} // namespace detail

} // namespace co_context

namespace co_context::detail {

struct io_context_meta_type {
    std::mutex mtx;
    std::condition_variable cv;
    config::ctx_id_t create_count; // Do not initialize this
    config::ctx_id_t ready_count;  // Do not initialize this
};

inline io_context_meta_type io_context_meta;

} // namespace co_context::detail
