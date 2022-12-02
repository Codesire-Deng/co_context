#include "co_context/co/condition_variable.hpp"
#include "co_context/io_context.hpp"

namespace co_context::detail {

void cv_wait_awaiter::await_suspend(std::coroutine_handle<> current) noexcept {
    this->lock_awaken_handle.register_coroutine(current);

    cv_wait_awaiter *old_head = cv.awaiting.load(std::memory_order_relaxed);
    do {
        this->next = old_head;
    } while (!cv.awaiting.compare_exchange_weak(
        old_head, this, std::memory_order_release, std::memory_order_relaxed
    ));

    this->lock_awaken_handle.unlock_ahead();
}

} // namespace co_context::detail