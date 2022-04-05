#pragma once
#include <coroutine>
#include <atomic>
#include "co_context/log/log.hpp"
#include "co_context/detail/thread_meta.hpp"

namespace liburingcxx {
class SQEntry;
class CQEntry;
}


namespace liburingcxx {
class SQEntry;
class CQEntry;
}

namespace co_context {

class counting_semaphore;
class condition_variable;

namespace detail {

    using liburingcxx::SQEntry;
    using liburingcxx::CQEntry;

    struct [[nodiscard]] task_info {
        union {
            SQEntry *sqe;
            CQEntry *cqe;
            int32_t result;
            counting_semaphore *sem;
            condition_variable *cv;
        };
        union {
            std::coroutine_handle<> handle;
            config::semaphore_counting_type update;
            config::condition_variable_counting_type notify_counter;
        };

        config::tid_t tid_hint;

        enum class task_type : uint8_t {
            sqe,
            cqe,
            result,
            co_spawn,
            semaphore_release,
            condition_variable_notify,
            nop
        };

        const task_type type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }
    };

    using task_info_ptr = task_info *;

} // namespace detail

} // namespace co_context
