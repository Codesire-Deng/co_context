#pragma once

#include <cstdint>

namespace liburingcxx {

class sq_entry;

} // namespace liburingcxx

namespace co_context::detail {

struct task_info;

struct submit_info {
    union {
        void *ptr;
        uintptr_t address;
        uintptr_t handle;
        uintptr_t sem_rel_task;
        uintptr_t cv_notify_task;
    };

    union {
        liburingcxx::sq_entry *available_sqe;
        liburingcxx::sq_entry *submit_sqe;
    };
};

enum submit_type : uint8_t { co_spawn, sem_rel, cv_notify };

/*
- submit:
  - eager_sqe:  0u64,       sqe
  - lazy_sqe:   0u64,       sqe
  - link_sqe:   0u64,       sqe
  - detach_sqe: 0u64,       sqe
  - co_spawn:   handle(0),  available_sqe
  - sem_rel:    task(1),    available_sqe
  - cv_noti:    task(2),    available_sqe
*/

/*
- available:
  - x,  available_sqe
  - x,  available_sqe
  - x,  available_sqe
*/

} // namespace co_context::detail
