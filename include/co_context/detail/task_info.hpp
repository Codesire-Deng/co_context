#pragma once
#include <coroutine>
#include "co_context/config.hpp"

#include "co_context/log/log.hpp"

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
            std::coroutine_handle<> handle;
            config::semaphore_counting_t update;
            config::condition_variable_counting_t notify_counter;
        };

        union {
            // SQEntry *sqe;
            // CQEntry *cqe;
            int32_t result;
            // counting_semaphore *sem;
            // condition_variable *cv;
        };

        union {
            config::tid_t tid_hint;
            config::eager_io_state_t eager_io_state; // for eager_io
        };

        enum class task_type : uint8_t {
            lazy_sqe,
            eager_sqe,
            // cqe,
            // result,
            co_spawn,
            semaphore_release,
            condition_variable_notify,
            nop
        };

        const task_type type;

        constexpr task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated\n");
        }

        uint64_t as_user_data() const noexcept {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        }
    };

    using task_info_ptr = task_info *;

} // namespace detail

} // namespace co_context
