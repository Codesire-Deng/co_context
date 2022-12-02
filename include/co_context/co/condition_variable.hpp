#pragma once

#include "co_context/co/mutex.hpp"
#include "co_context/detail/spinlock.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/worker_meta.hpp"
#include "co_context/task.hpp"
#include "co_context/utility/as_atomic.hpp"
#include "co_context/utility/bit.hpp"
#include <atomic>

namespace co_context {

class condition_variable;

} // namespace co_context

namespace co_context::detail {

class [[nodiscard("Did you forget to co_await?")]] cv_wait_awaiter final {
  public:
    using mutex = co_context::mutex;

    explicit cv_wait_awaiter(condition_variable & cv, mutex & mtx) noexcept
        : lock_awaken_handle(mtx.lock())
        , cv(cv) {
    }

    static constexpr bool await_ready() noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> current) noexcept;

    /**
     * @detail the lock must be held, when io_context resume me.
     * @detail io_context will call mutex::lock_awaiter::register_awaiting
     **/
    constexpr void await_resume() const noexcept {
    }

  private:
    mutex::lock_awaiter lock_awaken_handle;
    condition_variable &cv;
    // mutex &mtx;
    // std::coroutine_handle<> handle;

    cv_wait_awaiter *next = nullptr;
    friend class ::co_context::condition_variable;
    friend struct detail::worker_meta;
};

} // namespace co_context::detail

namespace co_context {

class condition_variable final {
  private:
    using cv_wait_awaiter = detail::cv_wait_awaiter;
    using task_info = detail::task_info;
    using T = config::condition_variable_counting_t;
    inline static constexpr T notify_all_flag = bit_top<T>;

  public:
    explicit condition_variable() noexcept = default;
    ~condition_variable() noexcept = default;

    cv_wait_awaiter wait(mutex &mtx) noexcept {
        return cv_wait_awaiter{*this, mtx};
    }

    template<std::predicate Pred>
    task<void> wait(mutex &mtx, Pred stop_waiting) {
        while (!stop_waiting()) {
            co_await this->wait(mtx);
        }
    }

    void notify_one() noexcept {
        auto *const worker = detail::this_thread.worker;
        assert(
            worker != nullptr
            && "condition_variable::notify_one() "
               "must run inside an io_context"
        );

        auto try_notify_one = [worker](cv_wait_awaiter *head) {
            auto &trylock_awaiter = head->lock_awaken_handle;
            if (!trylock_awaiter.register_awaiting()) [[unlikely]] {
                // lock succ, wakeup
                // TODO send to another io_context
                worker->co_spawn_unsafe(trylock_awaiter.awaken_coro);
            } else {
                // lock failed, just wait for another mutex.unlock()
            }
        };

        cv_wait_awaiter *head;

        notifier_mtx.lock();
        head = this->to_resume_head;
        if (this->to_resume_head != nullptr) {
            this->to_resume_head = this->to_resume_head->next;
            notifier_mtx.unlock();
            try_notify_one(head);
            return;
        }
        this->to_resume_tail = nullptr;
        notifier_mtx.unlock();

        head = awaiting.exchange(nullptr, std::memory_order_acquire);
        if (head == nullptr) [[unlikely]] {
            return;
        }

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
        if (this->to_resume_head == nullptr) [[likely]] {
            this->to_resume_head = succ;
            this->to_resume_tail = tail;
            notifier_mtx.unlock();
            try_notify_one(head);
            return;
        }
        this->to_resume_tail->next = head;
        cv_wait_awaiter *const to_notify = this->to_resume_head;
        this->to_resume_head = this->to_resume_head->next;
        notifier_mtx.unlock();
        try_notify_one(to_notify);
    }

    void notify_all() noexcept {
        auto *const worker = detail::this_thread.worker;
        assert(
            worker != nullptr
            && "condition_variable::notify_all() "
               "must run inside an io_context"
        );

        auto try_notify_all = [worker](cv_wait_awaiter *head) {
            while (head != nullptr) {
                auto &trylock_awaiter = head->lock_awaken_handle;
                if (!trylock_awaiter.register_awaiting()) [[unlikely]] {
                    // lock succ, wakeup
                    // TODO send to another io_context
                    worker->co_spawn_unsafe(trylock_awaiter.awaken_coro);
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

  private:
    friend class detail::cv_wait_awaiter;
    friend struct detail::worker_meta;

    std::atomic<cv_wait_awaiter *> awaiting{nullptr};
    cv_wait_awaiter *to_resume_head = nullptr;
    cv_wait_awaiter *to_resume_tail = nullptr;
    spinlock notifier_mtx;

    void to_resume_fetch_all() noexcept;
};

} // namespace co_context
