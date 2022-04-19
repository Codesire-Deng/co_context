#pragma once

#include "co_context/co/mutex.hpp"
#include "co_context/task.hpp"
#include "co_context/utility/bit.hpp"
#include "co_context/utility/as_atomic.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/detail/worker_meta.hpp"

namespace co_context {

class condition_variable;

namespace detail {

    class [[nodiscard("Did you forget to co_await?")]] cv_wait_awaiter final {
      public:
        using mutex = co_context::mutex;

        explicit cv_wait_awaiter(condition_variable & cv, mutex & mtx) noexcept
            : lock_awaken_handle(mtx.lock()), cv(cv), next(nullptr) {
        }

        constexpr bool await_ready() const noexcept {
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

        cv_wait_awaiter *next;
        friend class ::co_context::condition_variable;
        friend class ::co_context::io_context;
    };

} // namespace detail

class condition_variable final {
  private:
    using cv_wait_awaiter = detail::cv_wait_awaiter;
    using task_info = detail::task_info;
    using T = config::condition_variable_counting_t;
    inline static constexpr T notify_all_flag = bit_top<T>();

  public:
    condition_variable() noexcept
        : awaiting(nullptr)
        , to_resume_head(nullptr)
        , to_resume_tail(nullptr)
        , notify_task(task_info::task_type::condition_variable_notify) {
        // notify_task.cv = this; // deprecated
        as_atomic(notify_task.notify_counter)
            .store(0, std::memory_order_relaxed);
    }

    cv_wait_awaiter wait(mutex &mtx) noexcept {
        return cv_wait_awaiter{*this, mtx};
    }

    template<std::predicate Pred>
    task<void> wait(mutex &mtx, Pred stop_waiting) {
        while (!stop_waiting()) { co_await this->wait(mtx); }
    }

    void notify_one() noexcept {
        as_atomic(notify_task.notify_counter)
            .fetch_add(1, std::memory_order_relaxed);
        send_task();
    }

    void notify_all() noexcept {
        as_atomic(notify_task.notify_counter)
            .store(notify_all_flag, std::memory_order_relaxed);
        send_task();
    }

  private:
    friend class detail::cv_wait_awaiter;
    friend class io_context;

    std::atomic<cv_wait_awaiter *> awaiting;
    cv_wait_awaiter *to_resume_head;
    cv_wait_awaiter *to_resume_tail;

    task_info notify_task;

    void send_task() noexcept;
    void to_resume_fetch_all() noexcept;

  public:
    static consteval auto __task_offset() noexcept {
        return offsetof(condition_variable, notify_task);
    }
};

inline void condition_variable::send_task() noexcept {
    // register condition_variable-notify event, to io_context(worker)
    using namespace ::co_context::detail;
    auto *worker = this_thread.worker;
    assert(
        worker != nullptr
        && "condition_variable::send_task() must run inside an io_context"
    );
    log::d("condition_variable %lx notified\n", this);
    worker->submit_non_sqe(submit_info{.request = &notify_task});
}

} // namespace co_context
