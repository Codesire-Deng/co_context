#include "co_context/co/semaphore.hpp"
#include "co_context/io_context.hpp"
#include "co_context/log/log.hpp"
#include <cassert>

namespace co_context {

counting_semaphore::~counting_semaphore() noexcept {
    assert(awaiting.load(std::memory_order_relaxed) == nullptr);
    assert(to_resume == nullptr);
}

bool counting_semaphore::try_acquire() noexcept {
    T old_counter = counter.load(std::memory_order_relaxed);
    return old_counter > 0
           && counter.compare_exchange_strong(
               old_counter, old_counter - 1, std::memory_order_acquire,
               std::memory_order_relaxed
           );
}

inline static void send_task(detail::task_info *awaken_task) noexcept {
    using namespace co_context::detail;
    auto *worker = this_thread.worker;
    assert(
        worker != nullptr
        && "semaphore::release() must run inside an io_context"
    );
    worker->submit_non_sqe(
        reinterpret_cast<uintptr_t>(awaken_task) | submit_type::sem_rel
    );
}

void counting_semaphore::release(T update) noexcept {
    // register semaphore-update event, to io_context(worker)
    as_atomic(awaken_task.update).fetch_add(update, std::memory_order_relaxed);
    send_task(&awaken_task);
};

std::coroutine_handle<> counting_semaphore::try_release() noexcept {
    acquire_awaiter *resume_head = to_resume;
    if (resume_head == nullptr) {
        auto *node = awaiting.exchange(nullptr, std::memory_order_acquire);
        if (node == nullptr) {
            return nullptr; // no awaiting
        }

        do {
            acquire_awaiter *tmp = node->next;
            node->next = resume_head;
            resume_head = node;
            node = tmp;
        } while (node != nullptr);
    }

    assert(resume_head != nullptr);

    to_resume = resume_head->next;
    return resume_head->handle;
}

void counting_semaphore::acquire_awaiter::await_suspend(
    std::coroutine_handle<> current
) noexcept {
    this->handle = current;
    log::v("suspending coro: %lx\n", this->handle.address());
    // acquire failed
    acquire_awaiter *old_head = sem.awaiting.load(std::memory_order_acquire);
    do {
        this->next = old_head;
    } while (!sem.awaiting.compare_exchange_weak(
        old_head, this, std::memory_order_release, std::memory_order_relaxed
    ));
}

} // namespace co_context
