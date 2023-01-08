#pragma once

#include "co_context/config.hpp"
#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/task.hpp"
#include "co_context/utility/as_atomic.hpp"
#include "co_context/utility/mpl.hpp"
#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace co_context::detail {

template<typename... Ts>
constexpr bool is_all_void_v =
    (mpl::count_v<mpl::type_list<Ts...>, void> == sizeof...(Ts));

template<typename... Ts>
using variant_list =
    typename clear_void_t<Ts...>::template prepend<std::monostate>;

template<typename... Ts>
using to_any_variant_t =
    typename variant_list<Ts...>::template to<std::variant>;

template<typename... Ts>
using variant_or_uint =
    std::conditional_t<is_all_void_v<Ts...>, uint32_t, to_any_variant_t<Ts...>>;

template<typename... Ts>
using any_tuple = std::tuple<uint32_t, to_any_variant_t<Ts...>>;

template<typename... Ts>
using any_return_type =
    std::conditional_t<is_all_void_v<Ts...>, uint32_t, any_tuple<Ts...>>;

template<typename Variant>
struct any_meta {
    using result_type = Variant;

    result_type buffer;
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t idx{-1U};
    uint32_t count_down;

    explicit any_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : await_handle(await_handle)
        , count_down(n) {}

    result_type &as_result() & noexcept { return buffer; }
};

template<>
struct any_meta<uint32_t> {
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t idx{-1U};
    uint32_t count_down;

    explicit any_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : await_handle(await_handle)
        , count_down(n) {}
};

template<typename... Ts>
using any_meta_type = any_meta<variant_or_uint<Ts...>>;

template<safety is_thread_safe, size_t idx, typename... Ts>
task<void> evaluate_to(
    std::shared_ptr<any_meta_type<Ts...>> meta_ptr,
    task<mpl::select_t<idx, Ts...>> node // take the ownership
) {
    constexpr uint32_t n = sizeof...(Ts);
    using node_return_type = mpl::select_t<idx, Ts...>;

    bool is_cancelled;
    if constexpr (is_thread_safe) {
        is_cancelled =
            (as_atomic(meta_ptr->count_down).load(std::memory_order_relaxed)
             != n);
    } else {
        is_cancelled = (meta_ptr->count_down != n);
    }
    if (is_cancelled) {
        co_return;
    }

    auto preempt = [meta_ptr = meta_ptr.get()]() -> bool {
        if constexpr (is_thread_safe) {
            return as_atomic(meta_ptr->count_down)
                       .fetch_sub(1, std::memory_order_release)
                   == n;
        } else {
            return meta_ptr->count_down-- == n;
        }
    };

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
        if (preempt()) {
            meta_ptr->idx = idx;
            detail::co_spawn_handle(meta_ptr->await_handle);
        }
    } else {
        auto &&result = co_await node;
        if (preempt()) {
            meta_ptr->buffer = std::move(result);
            meta_ptr->idx = idx;
            detail::co_spawn_handle(meta_ptr->await_handle);
        }
    }
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, typename... Ts>
task<detail::any_return_type<Ts...>> any(task<Ts> &&...node) {
    constexpr uint32_t n = sizeof...(Ts);
    static_assert(n >= 2, "too few tasks for `any(...)`");

    using mate_type = detail::any_meta_type<Ts...>;
    auto meta_ptr = std::make_shared<mate_type>(co_await lazy::who_am_i(), n);

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(evaluate_to<is_thread_safe, idx, Ts...>(
                  meta_ptr, std::move(node)
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

    if constexpr (detail::is_all_void_v<Ts...>) {
        co_return meta_ptr->idx;
    } else {
        co_return detail::any_tuple<Ts...>{
            meta_ptr->idx, std::move(meta_ptr->buffer)};
    }
}

} // namespace co_context