#pragma once
#include <coroutine>
#include "co_context/config.hpp"

#include "co_context/log/log.hpp"

// TODO Delete me
// #include "uring/io_uring.h"
// #include "uring/cq_entry.hpp"

// namespace liburingcxx {
// class sq_entry;
// }

namespace co_context {

class counting_semaphore;
class condition_variable;

namespace detail {

    // using liburingcxx::sq_entry;
    // using liburingcxx::cq_entry;

    struct [[nodiscard]] task_info {
        union {
            std::coroutine_handle<> handle;
            config::semaphore_counting_t update;
            config::condition_variable_counting_t notify_counter;
        };

        union {
            // cq_entry *cqe;
            int32_t result;
            // counting_semaphore *sem;
            // condition_variable *cv;
        };

        union {
            config::tid_t tid_hint;
            config::eager_io_state_t eager_io_state; // for eager_io
        };

        enum class task_type : uint8_t {
            co_spawn,
            eager_sqe,
            lazy_sqe,
            lazy_link_sqe,
            // lazy_detached_sqe,
            // result,
            semaphore_release,
            condition_variable_notify,
            nop
        };

        task_type type;

        task_info(task_type type) noexcept : type(type) {
            log::v("task_info generated at %lx\n", this);
        }

        uint64_t as_user_data() const noexcept {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this));
        }

        uint64_t as_eager_user_data() const noexcept {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this))
                   | uint64_t(task_type::eager_sqe);
        }

        [[deprecated]] uint64_t as_linked_user_data() const noexcept {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this))
                   | uint64_t(task_type::lazy_link_sqe);
        }
    };

    static_assert(uint8_t(task_info::task_type::nop) < alignof(task_info));

    inline constexpr uintptr_t raw_task_info_mask =
        ~uintptr_t(alignof(task_info) - 1);

    static_assert((~raw_task_info_mask) == 0x7);

    inline task_info *raw_task_info_ptr(uintptr_t info) noexcept {
        return reinterpret_cast<task_info *>(info & raw_task_info_mask);
    }

} // namespace detail

} // namespace co_context
