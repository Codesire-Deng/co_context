#pragma once

#include <cstdint>
#include <coroutine>
#include <atomic>
#include "co_context/task_info.hpp"

namespace co_context {

class mutex final {
  private:
    using task_info = detail::task_info;

    class lock_awaiter final {
      public:
        explicit lock_awaiter(mutex &mtx) noexcept : mtx(mtx) {}

        constexpr bool await_ready() const noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> current) noexcept;
        constexpr void await_resume() const noexcept {}

      private:
        friend class mutex;
        mutex &mtx;
        lock_awaiter *next;
        task_info awaken_task{task_info::task_type::co_spawn};
    };

  public:
    /**
     * @brief Construct a new mutex that is not locked.
     */
    mutex() noexcept : awaiting(not_locked), to_resume(nullptr) {}

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

    /**
     * @brief Unlock the mutex.
     *
     * @note Must only be called by the current lock-holder. One of the waiting
     * coroutine will be resumed inside this call.
     */
    void unlock();

  private:
    inline static constexpr std::uintptr_t locked_no_awaiting = 0;
    inline static constexpr std::uintptr_t not_locked = 1;

    std::atomic<std::uintptr_t> awaiting;
    lock_awaiter *to_resume;
};

} // namespace co_context