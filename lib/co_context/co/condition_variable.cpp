#include "co_context/co/condition_variable.hpp"
#include "co_context.hpp"

namespace co_context {

namespace detail {

    void
    cv_wait_awaiter::await_suspend(std::coroutine_handle<> current) noexcept {
        this->lock_awaken_handle.register_coroutine(current);

        cv_wait_awaiter *old_head = cv.awaiting.load(std::memory_order_relaxed);
        do {
            this->next = old_head;
        } while (!cv.awaiting.compare_exchange_weak(
            old_head, this, std::memory_order_release,
            std::memory_order_relaxed));

        this->lock_awaken_handle.unlock_ahead();
    }

} // namespace detail

/**
 * @brief fetch all awaiting then append them to to_resume queue.
 * @note requires awaiting != nullptr !!
 */
void condition_variable::to_resume_fetch_all() noexcept {
    assert(awaiting.load(std::memory_order_relaxed) != nullptr);
    cv_wait_awaiter *node =
        awaiting.exchange(nullptr, std::memory_order_acquire);

    cv_wait_awaiter *fetch_head = nullptr;
    cv_wait_awaiter * const fetch_tail = node;

    do {
        cv_wait_awaiter *tmp = node->next;
        node->next = fetch_head;
        fetch_head = node;
        node = tmp;
    } while (node != nullptr);

    if (to_resume_tail == nullptr) {
        to_resume_head = fetch_head;
    } else {
        to_resume_tail->next = fetch_head;
    }
    to_resume_tail = fetch_tail;
}

} // namespace co_context