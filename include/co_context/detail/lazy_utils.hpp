#pragma once

#include "co_context/detail/worker_meta.hpp"
#include "co_context/io_context.hpp"
#include <coroutine>

namespace co_context::detail {

struct lazy_yield {
    static constexpr bool await_ready() noexcept { return false; }

    static void await_suspend(std::coroutine_handle<> current) noexcept {
        auto &worker = *detail::this_thread.worker;
        worker.co_spawn_unsafe(current);
    }

    constexpr void await_resume() const noexcept {}

    constexpr lazy_yield() noexcept = default;
};

struct lazy_who_am_i {
    static constexpr bool await_ready() noexcept { return false; }

    constexpr bool await_suspend(std::coroutine_handle<> current) noexcept {
        handle = current;
        return false;
    }

    [[nodiscard]]
    std::coroutine_handle<> await_resume() const noexcept {
        return handle;
    }

    constexpr lazy_who_am_i() noexcept = default;

    std::coroutine_handle<> handle;
};

using lazy_forget = std::suspend_always;

class lazy_resume_on {
  public:
    static constexpr bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> current) noexcept {
        resume_ctx.worker.co_spawn_auto(current);
    }

    constexpr void await_resume() const noexcept {}

    explicit lazy_resume_on(co_context::io_context &resume_context) noexcept
        : resume_ctx(resume_context) {}

  private:
    co_context::io_context &resume_ctx;
};

} // namespace co_context::detail

namespace co_context {

inline namespace lazy {

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_yield yield() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_who_am_i who_am_i() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_forget forget() noexcept {
        return {};
    }

    [[CO_CONTEXT_AWAIT_HINT]]
    inline detail::lazy_resume_on
    resume_on(co_context::io_context &resume_context) noexcept {
        return detail::lazy_resume_on{resume_context};
    }

} // namespace lazy

} // namespace co_context
