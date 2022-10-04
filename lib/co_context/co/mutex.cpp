#include "co_context/co/mutex.hpp"
#include "co_context/io_context.hpp"
#include <cassert>

namespace co_context {

mutex::~mutex() noexcept {
    [[maybe_unused]] auto state = awaiting.load(std::memory_order_relaxed);
    assert(state == not_locked || state == locked_no_awaiting);
    assert(to_resume == nullptr);
}

bool mutex::try_lock() noexcept {
    auto desire = not_locked;
    return awaiting.compare_exchange_strong(
        desire, locked_no_awaiting, std::memory_order_acquire,
        std::memory_order_relaxed
    );
}

inline static void send_task(std::coroutine_handle<> awaken_coro) noexcept {
    using namespace co_context::detail;
    auto *worker = this_thread.worker;
    assert(
        worker != nullptr && "mutex::unlock() must run inside an io_context"
    );
    worker->co_spawn(awaken_coro);
}

void mutex::unlock() noexcept {
    assert(awaiting.load(std::memory_order_relaxed) != not_locked);
    lock_awaiter *resume_head = to_resume;
    if (resume_head == nullptr) {
        auto desire = locked_no_awaiting;
        if (awaiting.compare_exchange_strong(
                desire, not_locked, std::memory_order_release,
                std::memory_order_relaxed
            )) {
            return; // no awaiting -> not locked & return
        }

        // There must be something awaiting now.
        auto top =
            awaiting.exchange(locked_no_awaiting, std::memory_order_acquire);

        assert(top != not_locked && top != locked_no_awaiting);

        auto *node = reinterpret_cast<lock_awaiter *>(top);
        do {
            lock_awaiter *tmp = node->next;
            node->next = resume_head;
            resume_head = node;
            node = tmp;
        } while (node != nullptr);
    }

    assert(resume_head != nullptr);

    to_resume = resume_head->next;
    send_task(resume_head->awaken_coro);
}

bool mutex::lock_awaiter::register_awaiting() noexcept {
    std::uintptr_t old_state = mtx.awaiting.load(std::memory_order_acquire);
    while (true) {
        if (old_state == mutex::not_locked) {
            if (mtx.awaiting.compare_exchange_weak(
                    old_state, mutex::locked_no_awaiting,
                    std::memory_order_acquire, std::memory_order_relaxed
                )) {
                return false; // lock succ, don't suspend.
            }
        } else {
            // try to push myself onto `awaiting` stack.
            this->next = reinterpret_cast<lock_awaiter *>(old_state);
            if (mtx.awaiting.compare_exchange_weak(
                    old_state, reinterpret_cast<uintptr_t>(this),
                    std::memory_order_release, std::memory_order_relaxed
                )) {
                return true; // wait for the mutex
            }
        }
    }
}

} // namespace co_context
