////////////////////////////////////////////////////////////////
// Reference: https://github.com/lewissbaker/cppcoro
// License: MIT License
// Modifications by Codesire-Deng
//
#pragma once

#include <co_context/detail/hint.hpp>
#include <co_context/detail/type_traits.hpp>
#include <co_context/io_context.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <memory>
#include <type_traits>

namespace co_context {

template<typename T>
class shared_task;

} // namespace co_context

namespace co_context::detail {

struct shared_task_waiter {
    std::coroutine_handle<> m_continuation;
    co_context::io_context *resume_ctx;
    shared_task_waiter *m_next;
};

class shared_task_promise_base {
    friend struct final_awaiter;

    struct final_awaiter {
        static constexpr bool await_ready() noexcept { return false; }

        template<typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept {
            shared_task_promise_base &promise = h.promise();

            // Exchange operation needs to be 'release' so that subsequent
            // awaiters have visibility of the result. Also needs to be
            // 'acquire' so we have visibility of writes to the waiters
            // list.
            void *const value_ready_value = &promise;
            void *waiters = promise.waiters_.exchange(
                value_ready_value, std::memory_order_acq_rel
            );
            if (waiters != nullptr) {
                auto *waiter = static_cast<shared_task_waiter *>(waiters);
                do {
                    waiter->resume_ctx->worker.co_spawn_auto(
                        waiter->m_continuation
                    );
                    waiter = waiter->m_next;
                } while (waiter != nullptr);
            }
        }

        void await_resume() noexcept {}
    };

  public:
    shared_task_promise_base() noexcept
        : ref_count_(1)
        , waiters_(&this->waiters_)
        , exception_(nullptr) {}

    constexpr std::suspend_always initial_suspend() noexcept { return {}; }

    final_awaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
    }

    [[nodiscard]]
    bool is_ready() const noexcept {
        const void *const value_ready_value = this;
        return waiters_.load(std::memory_order_acquire) == value_ready_value;
    }

    void add_ref() noexcept {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Decrement the reference count.
    ///
    /// \return
    /// true if successfully detached, false if this was the last
    /// reference to the coroutine, in which case the caller must
    /// call destroy() on the coroutine handle.
    bool try_detach() noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) != 1;
    }

    /// Try to enqueue a waiter to the list of waiters.
    ///
    /// \param waiter
    /// Pointer to the state from the waiter object.
    /// Must have waiter->coroutine_ member populated with the coroutine
    /// handle of the awaiting coroutine.
    ///
    /// \param coroutine
    /// Coroutine handle for this promise object.
    ///
    /// \return
    /// true if the waiter was successfully queued, in which case
    /// waiter->coroutine_ will be resumed when the task completes.
    /// false if the coroutine was already completed and the awaiting
    /// coroutine can continue without suspending.
    bool
    try_await(shared_task_waiter *waiter, std::coroutine_handle<> coroutine) {
        void *const value_ready_value = this;
        void *const not_started_value = &this->waiters_;
        constexpr void *started_no_waiters_value =
            static_cast<shared_task_waiter *>(nullptr);

        // NOTE: If the coroutine is not yet started then the first waiter
        // will start the coroutine before enqueuing itself up to the list
        // of suspended waiters waiting for completion. We split this into
        // two steps to allow the first awaiter to return without
        // suspending. This avoids recursively resuming the first waiter
        // inside the call to coroutine.resume() in the case that the
        // coroutine completes synchronously, which could otherwise lead to
        // stack-overflow if the awaiting coroutine awaited many
        // synchronously-completing tasks in a row.

        // Start the coroutine if not already started.
        void *old_waiters = waiters_.load(std::memory_order_acquire);
        if (old_waiters == not_started_value
            && waiters_.compare_exchange_strong(
                old_waiters, started_no_waiters_value, std::memory_order_relaxed
            )) {
            // Start the task executing.
            coroutine.resume();
            old_waiters = waiters_.load(std::memory_order_acquire);
        }

        // Enqueue the waiter into the list of waiting coroutines.
        do {
            if (old_waiters == value_ready_value) {
                // Coroutine already completed, don't suspend.
                return false;
            }

            waiter->m_next = static_cast<shared_task_waiter *>(old_waiters);
        } while (!waiters_.compare_exchange_weak(
            old_waiters, static_cast<void *>(waiter), std::memory_order_release,
            std::memory_order_acquire
        ));

        return true;
    }

  protected:
    bool completed_with_unhandled_exception() { return exception_ != nullptr; }

    void rethrow_if_unhandled_exception() {
        if (exception_ != nullptr) {
            std::rethrow_exception(exception_);
        }
    }

  private:
    std::atomic<std::uint32_t> ref_count_;

    // Value is either
    // - nullptr          - indicates started, no waiters
    // - this             - indicates value is ready
    // - &this->waiters_ - indicates coroutine not started
    // - other            - pointer to head item in linked-list of waiters.
    //                      values are of type
    //                      'co_context::shared_task_waiter'. indicates that
    //                      the coroutine has been started.
    std::atomic<void *> waiters_;

    std::exception_ptr exception_;
};

template<typename T>
class shared_task_promise : public shared_task_promise_base {
  public:
    shared_task_promise() noexcept = default;

    ~shared_task_promise() {
        if (this->is_ready() && !this->completed_with_unhandled_exception()) {
            reinterpret_cast<T *>(&value_storage_)->~T();
        }
    }

    shared_task<T> get_return_object() noexcept;

    template<typename VALUE>
        requires std::is_convertible_v<VALUE &&, T>
    void return_value(VALUE &&value
    ) noexcept(std::is_nothrow_constructible_v<T, VALUE &&>) {
        new (&value_storage_) T(std::forward<VALUE>(value));
    }

    T &result() {
        this->rethrow_if_unhandled_exception();
        return *reinterpret_cast<T *>(&value_storage_);
    }

  private:
    // Not using std::aligned_storage here due to bug in MSVC 2015 Update 2
    // that means it doesn't work for types with alignof(T) > 8.
    // See MS-Connect bug #2658635.
    alignas(T) char value_storage_[sizeof(T)];
};

template<>
class shared_task_promise<void> : public shared_task_promise_base {
  public:
    shared_task_promise() noexcept = default;

    shared_task<void> get_return_object() noexcept;

    void return_void() noexcept {}

    void result() { this->rethrow_if_unhandled_exception(); }
};

template<typename T>
class shared_task_promise<T &> : public shared_task_promise_base {
  public:
    shared_task_promise() noexcept = default;

    shared_task<T &> get_return_object() noexcept;

    void return_value(T &value) noexcept { value_ = std::addressof(value); }

    T &result() {
        this->rethrow_if_unhandled_exception();
        return *value_;
    }

  private:
    T *value_;
};
} // namespace co_context::detail

namespace co_context {

template<typename T = void>
class [[nodiscard]] shared_task {
  public:
    using promise_type = detail::shared_task_promise<T>;

    using value_type = T;

  private:
    struct awaitable_base {
        std::coroutine_handle<promise_type> coroutine_;
        detail::shared_task_waiter waiter_;

        awaitable_base(std::coroutine_handle<promise_type> coroutine) noexcept
            : coroutine_(coroutine) {}

        [[nodiscard]]
        bool await_ready() const noexcept {
            return !coroutine_ || coroutine_.promise().is_ready();
        }

        bool await_suspend(std::coroutine_handle<> awaiter) noexcept {
            waiter_.m_continuation = awaiter;
            waiter_.resume_ctx = detail::this_thread.ctx;
            assert(
                detail::this_thread.ctx != nullptr
                && "awaiting shared_task without an io_context"
            );
            return coroutine_.promise().try_await(&waiter_, coroutine_);
        }
    };

  public:
    shared_task() noexcept : coroutine_(nullptr) {}

    explicit shared_task(std::coroutine_handle<promise_type> coroutine)
        : coroutine_(coroutine) {
        // Don't increment the ref-count here since it has already been
        // initialised to 2 (one for shared_task and one for coroutine)
        // in the shared_task_promise constructor.
    }

    shared_task(shared_task &&other) noexcept : coroutine_(other.coroutine_) {
        other.coroutine_ = nullptr;
    }

    shared_task(const shared_task &other) noexcept
        : coroutine_(other.coroutine_) {
        if (coroutine_) {
            coroutine_.promise().add_ref();
        }
    }

    ~shared_task() { destroy(); }

    shared_task &operator=(shared_task &&other) noexcept {
        if (&other != this) {
            destroy();

            coroutine_ = other.coroutine_;
            other.coroutine_ = nullptr;
        }

        return *this;
    }

    // NOLINTNEXTLINE(cert-oop54-cpp, bugprone-unhandled-self-assignment)
    shared_task &operator=(const shared_task &other) noexcept {
        if (coroutine_ != other.coroutine_) {
            destroy();

            coroutine_ = other.coroutine_;

            if (coroutine_) {
                coroutine_.promise().add_ref();
            }
        }

        return *this;
    }

    void swap(shared_task &other) noexcept {
        std::swap(coroutine_, other.coroutine_);
    }

    /// \brief
    /// Query if the task result is complete.
    ///
    /// Awaiting a task that is ready will not block.
    [[nodiscard]]
    bool is_ready() const noexcept {
        return !coroutine_ || coroutine_.promise().is_ready();
    }

    auto operator co_await() const noexcept {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            decltype(auto) await_resume() {
                // if (!this->handle) [[unlikely]]
                //     throw std::logic_error("broken_promise");
                assert(this->coroutine_ && "broken_promise");

                return this->coroutine_.promise().result();
            }
        };

        return awaitable{coroutine_};
    }

    /// \brief
    /// Returns an awaitable that will await completion of the task without
    /// attempting to retrieve the result.
    auto when_ready() const noexcept {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            void await_resume() const noexcept {}
        };

        return awaitable{coroutine_};
    }

    std::coroutine_handle<promise_type> get_handle() const noexcept {
        return coroutine_;
    }

  private:
    template<typename U>
    friend bool
    operator==(const shared_task<U> &, const shared_task<U> &) noexcept;

    void destroy() noexcept {
        if (coroutine_) {
            if (!coroutine_.promise().try_detach()) {
                coroutine_.destroy();
            }
        }
    }

    std::coroutine_handle<promise_type> coroutine_;
};

template<typename T>
bool operator==(const shared_task<T> &lhs, const shared_task<T> &rhs) noexcept {
    return lhs.coroutine_ == rhs.coroutine_;
}

template<typename T>
bool operator!=(const shared_task<T> &lhs, const shared_task<T> &rhs) noexcept {
    return !(lhs == rhs);
}

template<typename T>
void swap(shared_task<T> &a, shared_task<T> &b) noexcept {
    a.swap(b);
}

namespace detail {

    template<typename T>
    shared_task<T> shared_task_promise<T>::get_return_object() noexcept {
        return shared_task<T>{
            std::coroutine_handle<shared_task_promise>::from_promise(*this)
        };
    }

    template<typename T>
    shared_task<T &> shared_task_promise<T &>::get_return_object() noexcept {
        return shared_task<T &>{
            std::coroutine_handle<shared_task_promise>::from_promise(*this)
        };
    }

    inline shared_task<void>
    shared_task_promise<void>::get_return_object() noexcept {
        return shared_task<void>{
            std::coroutine_handle<shared_task_promise>::from_promise(*this)
        };
    }

} // namespace detail

template<typename AWAITABLE>
auto make_shared_task(AWAITABLE awaitable)
    -> shared_task<detail::remove_rvalue_reference_t<
        detail::get_awaiter_result_t<AWAITABLE>>> {
    co_return co_await static_cast<AWAITABLE &&>(awaitable);
}

/*
inline void io_context::co_spawn(shared_task<void> &entrance) noexcept {
    this->co_spawn<safety::safe>(entrance);
}

template<safety is_thread_safe>
inline void io_context::co_spawn(shared_task<void> &entrance) noexcept {
    auto handle = entrance.get_handle();
    if constexpr (is_thread_safe) {
        worker.co_spawn_auto(handle);
    } else {
        worker.co_spawn_unsafe(handle);
    }
}

inline void co_spawn(shared_task<void> &entrance) noexcept {
    assert(
        detail::this_thread.ctx != nullptr
        && "Can not co_spawn() on the thread "
           "without a running io_context!"
    );
    auto handle = entrance.get_handle();
    detail::this_thread.worker->co_spawn_unsafe(handle);
}
*/

} // namespace co_context
