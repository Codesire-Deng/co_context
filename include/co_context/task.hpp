#pragma once

#include <co_context/detail/attributes.hpp>
#include <co_context/detail/type_traits.hpp>

#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>

namespace co_context {

/**
 * @brief A task<> is a lazy synchronous coroutine that only executes at
 * co_await <task> (or co_await <task>.when_ready()).
 * @note As long as a task<> has been awaited, it will execute immediately at
 * current thread, and will not return until it's all finished.
 * @tparam T
 */
template<typename T>
class task;

namespace detail {
    template<typename T>
    class task_promise_base;

    /**
     * @brief When current task<> finishes, resume its parent.
     */
    template<typename T>
    struct task_final_awaiter {
        static constexpr bool await_ready() noexcept { return false; }

        template<std::derived_from<task_promise_base<T>> Promise>
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> current) noexcept {
            return current.promise().parent_coro;
        }

        // Won't be resumed anyway
        constexpr void await_resume() const noexcept {}
    };

    /**
     * @brief When current task<> finishes, resume its parent.
     */
    template<>
    struct task_final_awaiter<void> {
        static constexpr bool await_ready() noexcept { return false; }

        template<std::derived_from<task_promise_base<void>> Promise>
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<Promise> current) noexcept {
            auto &promise = current.promise();

            std::coroutine_handle<> continuation = promise.parent_coro;

            if (promise.is_detached_flag == Promise::is_detached) {
                current.destroy();
            }

            return continuation;
        }

        // Won't be resumed anyway
        constexpr void await_resume() const noexcept {}
    };

    /**
     * @brief Define the behavior of all tasks.
     *
     * final_suspend: yes, and return to parent
     */
    template<typename T>
    class task_promise_base {
        friend struct task_final_awaiter<T>;

      public:
        task_promise_base() noexcept = default;

        inline constexpr std::suspend_always initial_suspend() noexcept {
            return {};
        }

        inline constexpr task_final_awaiter<T> final_suspend() noexcept {
            return {};
        }

        inline void set_parent(std::coroutine_handle<> continuation) noexcept {
            parent_coro = continuation;
        }

        task_promise_base(const task_promise_base &) = delete;
        task_promise_base(task_promise_base &&) = delete;
        task_promise_base &operator=(const task_promise_base &) = delete;
        task_promise_base &operator=(task_promise_base &&) = delete;

      private:
        std::coroutine_handle<> parent_coro{std::noop_coroutine()};
    };

    /**
     * @brief task<> with a return value
     *
     * @tparam T the type of the final result
     */
    template<typename T>
    class task_promise final : public task_promise_base<T> {
      public:
        task_promise() noexcept : state(value_state::mono){};

        ~task_promise() {
            switch (state) {
                [[likely]] case value_state::value:
                    value.~T();
                    break;
                case value_state::exception:
                    exception_ptr.~exception_ptr();
                    break;
                default: break;
            }
        };

        task<T> get_return_object() noexcept;

        void unhandled_exception() noexcept {
            exception_ptr = std::current_exception();
            state = value_state::exception;
        }

        template<typename Value>
            requires std::convertible_to<Value &&, T>
        void return_value(Value &&result
        ) noexcept(std::is_nothrow_constructible_v<T, Value &&>) {
            std::construct_at(
                std::addressof(value), std::forward<Value>(result)
            );
            state = value_state::value;
        }

        // get the lvalue ref
        T &result() & {
            if (state == value_state::exception) [[unlikely]] {
                std::rethrow_exception(exception_ptr);
            }
            assert(state == value_state::value);
            return value;
        }

        // get the prvalue
        T &&result() && {
            if (state == value_state::exception) [[unlikely]] {
                std::rethrow_exception(exception_ptr);
            }
            assert(state == value_state::value);
            return std::move(value);
        }

      private:
        union {
            T value;
            std::exception_ptr exception_ptr;
        };
        enum class value_state : uint8_t { mono, value, exception } state;
    };

    template<>
    class task_promise<void> final : public task_promise_base<void> {
        friend struct task_final_awaiter<void>;
        friend class task<void>;

      public:
        task_promise() noexcept : is_detached_flag(0){};

        ~task_promise() noexcept {
            if (is_detached_flag != is_detached) {
                exception_ptr.~exception_ptr();
            }
        }

        task<void> get_return_object() noexcept;

        constexpr void return_void() noexcept {}

        void unhandled_exception() {
            if (is_detached_flag == is_detached) {
                std::rethrow_exception(std::current_exception());
            } else {
                exception_ptr = std::current_exception();
            }
        }

        void result() const {
            if (this->exception_ptr) [[unlikely]] {
                std::rethrow_exception(this->exception_ptr);
            }
        }

      private:
        inline static constexpr uintptr_t is_detached = -1ULL;

        union {
            uintptr_t is_detached_flag; // set to `is_detached` if is detached.
            std::exception_ptr exception_ptr;
        };
    }; // namespace co_context

    template<typename T>
    class task_promise<T &> final : public task_promise_base<T &> {
      public:
        task_promise() noexcept = default;

        task<T &> get_return_object() noexcept;

        void unhandled_exception() noexcept {
            this->exception_ptr = std::current_exception();
        }

        void return_value(T &result) noexcept {
            value = std::addressof(result);
        }

        T &result() {
            if (exception_ptr) [[unlikely]] {
                std::rethrow_exception(exception_ptr);
            }
            return *value;
        }

      private:
        T *value = nullptr;
        std::exception_ptr exception_ptr;
    };

} // namespace detail

template<typename T = void>
class [[CO_CONTEXT_AWAIT_HINT]] task {
  public:
    using promise_type = detail::task_promise<T>;
    using value_type = T;

    struct is_tasklike {};

  private:
    struct awaiter_base {
        std::coroutine_handle<promise_type> handle;

        explicit awaiter_base(std::coroutine_handle<promise_type> current
        ) noexcept
            : handle(current) {}

        [[nodiscard]]
        inline bool await_ready() const noexcept {
            return !handle || handle.done();
        }

        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<> awaiting_coro) noexcept {
            handle.promise().set_parent(awaiting_coro);
            return handle;
        }
    };

  public:
    task() noexcept = default;

    explicit task(std::coroutine_handle<promise_type> current) noexcept
        : handle(current) {}

    task(task &&other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    // Ban copy
    task(const task<> &) = delete;
    task &operator=(const task &) = delete;

    task &operator=(task &&other) noexcept {
        if (this != std::addressof(other)) [[likely]] {
            if (handle) {
                handle.destroy();
            }
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    // Free the promise object and coroutine parameters
    ~task() {
        if (handle) {
            handle.destroy();
        }
    }

    [[nodiscard]]
    inline bool is_ready() const noexcept {
        return !handle || handle.done();
    }

    /**
     * @brief wait for the task<> to complete, and get the ref of the result
     */
    auto operator co_await() const & noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            decltype(auto) await_resume() {
                // if (!this->handle) [[unlikely]]
                //     throw std::logic_error("broken_promise");
                assert(this->handle && "broken_promise");

                return this->handle.promise().result();
            }
        };

        return awaiter{handle};
    }

    /**
     * @brief wait for the task<> to complete, and get the rvalue ref of the
     * result
     */
    auto operator co_await() const && noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            decltype(auto) await_resume() {
                // if (!this->handle) [[unlikely]]
                //     throw std::logic_error("broken_promise");
                assert(this->handle && "broken_promise");

                return std::move(this->handle.promise()).result();
            }
        };

        return awaiter{handle};
    }

    /**
     * @brief wait for the task<> to complete, but do not get the result
     */
    [[nodiscard]]
    auto when_ready() const noexcept {
        struct awaiter : awaiter_base {
            using awaiter_base::awaiter_base;

            constexpr void await_resume() const noexcept {}
        };

        return awaiter{handle};
    }

    std::coroutine_handle<promise_type> get_handle() noexcept { return handle; }

    void detach() noexcept {
        if constexpr (std::is_void_v<value_type>) {
            handle.promise().is_detached_flag = promise_type::is_detached;
        }
        handle = nullptr;
    }

    friend void swap(task &a, task &b) noexcept {
        std::swap(a.handle, b.handle);
    }

  private:
    std::coroutine_handle<promise_type> handle;
};

namespace detail {

    template<typename T>
    inline task<T> task_promise<T>::get_return_object() noexcept {
        return task<T>{std::coroutine_handle<task_promise>::from_promise(*this)
        };
    }

    inline task<void> task_promise<void>::get_return_object() noexcept {
        return task<void>{
            std::coroutine_handle<task_promise>::from_promise(*this)
        };
    }

    template<typename T>
    inline task<T &> task_promise<T &>::get_return_object() noexcept {
        return task<T &>{std::coroutine_handle<task_promise>::from_promise(*this
        )};
    }

} // namespace detail

template<typename Awaiter>
auto make_task(Awaiter awaiter) -> task<
    detail::remove_rvalue_reference_t<detail::get_awaiter_result_t<Awaiter>>> {
    co_return co_await static_cast<Awaiter &&>(awaiter);
}

} // namespace co_context
