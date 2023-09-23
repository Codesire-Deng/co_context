#pragma once

#include <co_context/detail/attributes.hpp>
#include <co_context/detail/spinlock.hpp>
#include <co_context/detail/thread_meta.hpp>
#include <co_context/log/log.hpp>
#include <co_context/utility/as_atomic.hpp>

#include <coroutine>
#include <type_traits>

namespace co_context {

class io_context;

class counting_semaphore final {
  private:
    using T = config::semaphore_counting_t;
    static_assert(std::is_integral_v<T>);

    class [[CO_CONTEXT_AWAIT_HINT]] acquire_awaiter final {
      public:
        explicit acquire_awaiter(counting_semaphore &sem) noexcept
            : sem(sem)
            , resume_ctx(detail::this_thread.ctx) {}

        bool await_ready() noexcept {
            const T old_counter =
                sem.counter.fetch_sub(1, std::memory_order_acquire);
            return old_counter > 0;
        }

        void await_suspend(std::coroutine_handle<> current) noexcept;

        void await_resume() const noexcept {}

      private:
        void co_spawn() const noexcept;

        counting_semaphore &sem;
        acquire_awaiter *next = nullptr;
        std::coroutine_handle<> handle;
        co_context::io_context *resume_ctx;
        friend class counting_semaphore;
        friend struct detail::worker_meta;
    };

  public:
    explicit counting_semaphore(T desired) noexcept
        : awaiting(nullptr)
        , counter(desired) {}

    counting_semaphore(const counting_semaphore &) = delete;

    ~counting_semaphore() noexcept;

    bool try_acquire() noexcept;

    acquire_awaiter acquire() noexcept {
        log::v("semaphore %lx acquiring\n", this);
        return acquire_awaiter{*this};
    }

    // release one. faster than release(1).
    void release() noexcept;

    void release(T update) noexcept;

  private:
    friend struct detail::worker_meta;
    acquire_awaiter *try_release() noexcept;

  private:
    std::atomic<acquire_awaiter *> awaiting;
    acquire_awaiter *to_resume = nullptr;
    std::atomic<T> counter;
    detail::spinlock notifier_mtx;
};

} // namespace co_context
