#pragma once

#include <co_context/detail/hint.hpp>
#include <co_context/detail/io_context_meta.hpp>
#include <co_context/detail/lock_guard.hpp>
#include <co_context/detail/thread_meta.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>

namespace co_context {

class io_context;

namespace detail {
    class cv_wait_awaiter;
} // namespace detail

class mutex final {
  public:
    class [[CO_CONTEXT_AWAIT_HINT]] lock_awaiter {
      public:
        explicit lock_awaiter(mutex &mtx) noexcept
            : mtx(mtx)
            , resume_ctx(detail::this_thread.ctx) {}

        static constexpr bool await_ready() noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> current) noexcept {
            register_coroutine(current);
            return register_awaiting();
        }

        void await_resume() const noexcept {}

      protected:
        void register_coroutine(std::coroutine_handle<> handle) noexcept {
            awaken_coro = handle;
        }

        std::coroutine_handle<> get_coroutine() noexcept { return awaken_coro; }

        /**
         * @brief lock, and when it needs, register handle to awaiting list
         * @return if the coro needs to suspend
         */
        bool register_awaiting() noexcept;

        // reserved for condition_variable
        void unlock_ahead() noexcept { mtx.unlock(); }

        void co_spawn() const noexcept;

      protected:
        mutex &mtx;
        lock_awaiter *next = nullptr;
        std::coroutine_handle<> awaken_coro;
        co_context::io_context *resume_ctx;
        friend class co_context::mutex;
        friend class detail::cv_wait_awaiter;
        friend class co_context::condition_variable;
        friend struct detail::worker_meta;
    };

    class [[CO_CONTEXT_AWAIT_HINT]] lock_guard_awaiter final
        : public lock_awaiter {
      private:
        friend class mutex;

        using lock_guard = detail::lock_guard<mutex>;

      public:
        using lock_awaiter::lock_awaiter;

        [[nodiscard]]
        lock_guard await_resume() const noexcept {
            return lock_guard{mtx};
        }
    };

  public:
    /**
     * @brief Construct a new mutex that is not locked.
     */
    mutex() noexcept : awaiting(not_locked) {}

    /**
     * @brief Destroy the mutex.
     *
     * @note The behavior is undefined if the mutex is owned by any coro or if
     * any coro terminates while holding any ownership of the mutex.
     */
    ~mutex() noexcept;

    /**
     * @brief Attemp to acquire a lock on the mutex without awaiting.
     *
     * @return True if the lock was acquired successfully, then unlock() should
     * be called at some point.  False if the mutex already locked.
     */
    bool try_lock() noexcept;

    /**
     * @brief Acquire a lock on the mutex. If the lock could not be acquired
     * synchronous then the awaiting coroutine will be suspended an later
     * resumed when the lock becomes available.
     *
     * @return A lock_awaiter that must(should) be `co_await`ed to wait until
     * the lock is acquired. Type of `co_await m.lock()` is `void`.
     */
    lock_awaiter lock() noexcept { return lock_awaiter{*this}; }

    lock_guard_awaiter lock_guard() noexcept {
        return lock_guard_awaiter{*this};
    }

    /**
     * @brief Unlock the mutex.
     *
     * @note Must only be called by the current lock-holder. One of the waiting
     * coroutine will be resumed inside this call.
     */
    void unlock() noexcept;

  private:
    inline static constexpr std::uintptr_t locked_no_awaiting = 0;
    inline static constexpr std::uintptr_t not_locked = 1;

    std::atomic<std::uintptr_t> awaiting;
    lock_awaiter *to_resume = nullptr;
};

} // namespace co_context
