#pragma once

#include <co_context/co/mutex.hpp>
#include <co_context/detail/attributes.hpp>
#include <co_context/detail/spinlock.hpp>
#include <co_context/detail/thread_meta.hpp>
#include <co_context/detail/trival_task.hpp>
#include <co_context/detail/worker_meta.hpp>
#include <co_context/utility/as_atomic.hpp>
#include <co_context/utility/bit.hpp>

#include <atomic>

namespace co_context {

class condition_variable;

} // namespace co_context

namespace co_context::detail {

class [[CO_CONTEXT_AWAIT_HINT]] cv_wait_awaiter final {
  public:
    using mutex = co_context::mutex;

    explicit cv_wait_awaiter(condition_variable &cv, mutex &mtx) noexcept
        : lock_awaken_handle(mtx.lock())
        , cv(cv) {}

    static constexpr bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> current) noexcept;

    /**
     * @detail the lock must be held, when io_context resume me.
     * @detail io_context will call mutex::lock_awaiter::register_awaiting
     **/
    constexpr void await_resume() const noexcept {}

  private:
    mutex::lock_awaiter lock_awaken_handle;
    condition_variable &cv;

    cv_wait_awaiter *next = nullptr;
    friend class ::co_context::condition_variable;
    friend struct detail::worker_meta;
};

} // namespace co_context::detail

namespace co_context {

class condition_variable final {
  private:
    using cv_wait_awaiter = detail::cv_wait_awaiter;
    using T = config::condition_variable_counting_t;
    inline static constexpr T notify_all_flag = bit_top<T>;

  public:
    explicit condition_variable() noexcept = default;
    ~condition_variable() noexcept = default;

    cv_wait_awaiter wait(mutex &mtx) noexcept {
        return cv_wait_awaiter{*this, mtx};
    }

    template<std::predicate Pred>
    detail::trival_task wait(mutex &mtx, Pred stop_waiting) {
        while (!stop_waiting()) {
            co_await this->wait(mtx);
        }
    }

    void notify_one() noexcept;

    void notify_all() noexcept;

  private:
    friend class detail::cv_wait_awaiter;
    friend struct detail::worker_meta;

    std::atomic<cv_wait_awaiter *> awaiting{nullptr};
    cv_wait_awaiter *to_resume_head = nullptr;
    cv_wait_awaiter *to_resume_tail = nullptr;
    detail::spinlock notifier_mtx;
};

} // namespace co_context
