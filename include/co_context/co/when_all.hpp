#pragma once

#include <co_context/config.hpp>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/task.hpp>
#include <co_context/utility/as_atomic.hpp>
#include <co_context/utility/mpl.hpp>

#include <coroutine>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace co_context::detail {

template<typename... Ts>
using tuple_or_void = std::conditional_t<
    std::is_same_v<std::tuple<>, mpl::remove_t<void, Ts...>>,
    void,
    mpl::remove_t<void, Ts...>>;

template<typename... Ts>
struct all_meta {
    using result_type = std::tuple<Ts...>;
    using buffer_type = std::tuple<mpl::uninitialized<Ts>...>;
    using result_type_list = mpl::type_list<Ts...>;

    static_assert(sizeof...(Ts) != 0);
    static_assert(mpl::count_v<result_type_list, void> == 0);

    buffer_type buffer;
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t count_down;

    explicit all_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : await_handle(await_handle)
        , count_down(n) {}

    ~all_meta() noexcept(noexcept(std::destroy_at(&as_result()))) {
        std::destroy_at(&as_result());
    }

    result_type &as_result() & noexcept {
        return *reinterpret_cast<result_type *>(&buffer);
    }
};

template<>
struct all_meta<> {
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t count_down;

    explicit all_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : await_handle(await_handle)
        , count_down(n) {}
};

template<typename... Ts>
using to_all_meta_t = typename clear_void_t<Ts...>::template to<all_meta>;

template<safety is_thread_safe, size_t idx, typename... Ts>
task<void> all_evaluate_to(
    to_all_meta_t<Ts...> &meta, task<mpl::select_t<idx, Ts...>> &&node
) {
    using node_return_type = mpl::select_t<idx, Ts...>;

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
    } else {
        using list = mpl::first_N_t<mpl::type_list<Ts...>, idx + 1>;

        constexpr size_t pos = idx - mpl::count_v<list, void>;

        auto *const location =
            reinterpret_cast<node_return_type *>(std::get<pos>(meta.buffer).data
            );

        std::construct_at(location, std::move(co_await std::move(node)));
    }

    bool wakeup;
    if constexpr (is_thread_safe) {
        wakeup =
            (as_atomic(meta.count_down).fetch_sub(1, std::memory_order_relaxed)
             == 1);
    } else {
        wakeup = (--meta.count_down == 0);
    }
    if (wakeup) {
        if constexpr (is_thread_safe) {
            std::atomic_thread_fence(std::memory_order_release);
        }
        detail::co_spawn_handle(meta.await_handle);
    }
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, typename... Ts>
task<detail::tuple_or_void<Ts...>> all(task<Ts>... node) {
    constexpr size_t n = sizeof...(Ts);
    static_assert(n >= 2, "too few tasks for `all(...)`");

    using meta_type = detail::to_all_meta_t<Ts...>;
    meta_type meta{co_await lazy::who_am_i(), n};

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(all_evaluate_to<is_thread_safe, idx, Ts...>(
                  meta, std::move(node)
              )));
    };

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_release);
    }

    spawn_all(std::index_sequence_for<Ts...>{});

    co_await lazy::forget();

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    if constexpr (std::is_void_v<detail::tuple_or_void<Ts...>>) {
        co_return;
    } else {
        co_return std::move(meta.as_result());
    }
}

} // namespace co_context
