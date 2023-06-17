#include <co_context/co/condition_variable.hpp>
#include <co_context/io_context.hpp>

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

namespace co_context {

void condition_variable::notify_one() noexcept {
    auto try_notify_one = [](cv_wait_awaiter *head) {
        auto &awaken_awaiter = head->lock_awaken_handle;
        if (!awaken_awaiter.register_awaiting()) [[unlikely]] {
            // lock succ, wakeup
            awaken_awaiter.co_spawn();
        } else {
            // lock failed, just wait for another mutex.unlock()
        }
    };

    cv_wait_awaiter *head;

    // 1. try to notify the head of `to_resume` linked list.
    notifier_mtx.lock();
    head = this->to_resume_head;
    if (this->to_resume_head != nullptr) {
        this->to_resume_head = this->to_resume_head->next;
        notifier_mtx.unlock();
        try_notify_one(head);
        return;
    }
    // 2. `to_resume` linked list is empty, then check the `awaiting` list.
    this->to_resume_tail = nullptr;
    notifier_mtx.unlock();

    head = awaiting.exchange(nullptr, std::memory_order_acquire);
    if (head == nullptr) [[unlikely]] {
        return;
    }

    // 3. reverse the `awaiting` list as `head`.
    cv_wait_awaiter *const tail = head;
    cv_wait_awaiter *succ = nullptr;
    cv_wait_awaiter *pred = head->next;
    while (pred != nullptr) {
        head->next = succ;
        succ = head;
        head = pred;
        pred = pred->next;
    }

    notifier_mtx.lock();
    // 4. if `to_resume` is empty, it becomes succ->...->tail.
    if (this->to_resume_head == nullptr) [[likely]] {
        this->to_resume_head = succ;
        // `tail` may not be nullptr, even if `succ` is nullptr.
        this->to_resume_tail = tail;
        notifier_mtx.unlock();
        try_notify_one(head);
        return;
    }
    // 5. if `to_resume` is not empty, append head->...->tail to it.
    this->to_resume_tail->next = head;
    cv_wait_awaiter *const to_notify = this->to_resume_head;
    this->to_resume_head = this->to_resume_head->next;
    this->to_resume_tail = tail;
    notifier_mtx.unlock();
    try_notify_one(to_notify);
}

void condition_variable::notify_all() noexcept {
    auto try_notify_all = [](cv_wait_awaiter *head) {
        while (head != nullptr) {
            auto &awaken_awaiter = head->lock_awaken_handle;
            if (!awaken_awaiter.register_awaiting()) [[unlikely]] {
                // lock succ, wakeup
                awaken_awaiter.co_spawn();
            } else {
                // lock failed, just wait for another mutex.unlock()
            }
            head = head->next;
        }
    };

    cv_wait_awaiter *resume_head =
        awaiting.exchange(nullptr, std::memory_order_acquire);

    try_notify_all(resume_head);

    notifier_mtx.lock();
    resume_head = this->to_resume_head;
    this->to_resume_head = nullptr;
    // this->to_resume_tail = nullptr;
    notifier_mtx.unlock();

    try_notify_all(resume_head);
}

} // namespace co_context
