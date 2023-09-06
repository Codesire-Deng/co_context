#pragma once

#include <co_context/config.hpp>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/task.hpp>
#include <co_context/utility/as_atomic.hpp>
#include <co_context/utility/mpl.hpp>

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
    uint32_t finish_count{0};

    explicit any_meta(std::coroutine_handle<> await_handle) noexcept
        : await_handle(await_handle) {}

    result_type &as_result() & noexcept { return buffer; }
};

template<>
struct any_meta<uint32_t> {
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t idx{-1U};
    uint32_t finish_count{0};

    explicit any_meta(std::coroutine_handle<> await_handle) noexcept
        : await_handle(await_handle) {}
};

template<typename... Ts>
using any_meta_type = any_meta<variant_or_uint<Ts...>>;

template<safety is_thread_safe, size_t idx, typename... Ts>
task<void> any_evaluate_to(
    std::shared_ptr<any_meta_type<Ts...>> meta_ptr,
    task<mpl::select_t<idx, Ts...>> node // take the ownership
) {
    constexpr uint32_t n = sizeof...(Ts);
    using node_return_type = mpl::select_t<idx, Ts...>;

    bool is_cancelled;
    if constexpr (is_thread_safe) {
        is_cancelled =
            (as_atomic(meta_ptr->finish_count).load(std::memory_order_relaxed)
             != 0);
    } else {
        is_cancelled = (meta_ptr->finish_count != 0);
    }
    if (is_cancelled) {
        co_return;
    }

    auto preempt = [meta_ptr = meta_ptr.get()]() -> bool {
        if constexpr (is_thread_safe) {
            return as_atomic(meta_ptr->finish_count)
                       .fetch_add(1, std::memory_order_acquire)
                   == 0;
        } else {
            return meta_ptr->finish_count++ == 0;
        }
    };

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
        if (preempt()) {
            meta_ptr->idx = idx;
            if constexpr (is_thread_safe) {
                std::atomic_thread_fence(std::memory_order_release);
            }
            detail::co_spawn_handle(meta_ptr->await_handle);
        }
    } else {
        auto &&result = co_await node;
        if (preempt()) {
            meta_ptr->buffer = std::move(result);
            meta_ptr->idx = idx;
            if constexpr (is_thread_safe) {
                std::atomic_thread_fence(std::memory_order_release);
            }
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

    using meta_type = detail::any_meta_type<Ts...>;
    auto meta_ptr = std::make_shared<meta_type>(co_await lazy::who_am_i());

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(any_evaluate_to<is_thread_safe, idx, Ts...>(
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
            meta_ptr->idx, std::move(meta_ptr->buffer)
        };
    }
}
} // namespace co_context

namespace co_context::detail {

template<typename... Ts>
using some_return_type = std::vector<any_return_type<Ts...>>;

template<typename... Ts>
struct some_meta {
    using result_type = some_return_type<Ts...>;

    result_type buffer;
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t idx{0};
    uint32_t min_complete;

    explicit some_meta(
        std::coroutine_handle<> await_handle, uint32_t min_complete
    ) noexcept
        : await_handle(await_handle)
        , min_complete(min_complete) {
        buffer.resize(min_complete);
    }

    result_type &as_result() & noexcept { return buffer; }
};

template<safety is_thread_safe, size_t idx, typename... Ts>
task<void> some_evaluate_to(
    const uint32_t min_complete,
    std::shared_ptr<some_meta<Ts...>> meta_ptr,
    task<mpl::select_t<idx, Ts...>> node // take the ownership
) {
    constexpr uint32_t n = sizeof...(Ts);
    using node_return_type = mpl::select_t<idx, Ts...>;

    bool is_cancelled;
    if constexpr (is_thread_safe) {
        is_cancelled =
            (as_atomic(meta_ptr->idx).load(std::memory_order_relaxed)
             >= min_complete);
    } else {
        is_cancelled = (meta_ptr->idx >= min_complete);
    }
    if (is_cancelled) {
        co_return;
    }

    auto preempt = [meta_ptr = meta_ptr.get()]() -> uint32_t {
        if constexpr (is_thread_safe) {
            return as_atomic(meta_ptr->idx)
                .fetch_add(1, std::memory_order_acquire);
        } else {
            return meta_ptr->idx++;
        }
    };

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
        const uint32_t rank = preempt();
        if (rank < min_complete) {
            std::get<0>(meta_ptr->buffer[rank]) = idx;
            if (rank == min_complete) {
                if constexpr (is_thread_safe) {
                    std::atomic_thread_fence(std::memory_order_release);
                }
                detail::co_spawn_handle(meta_ptr->await_handle);
            }
        }
    } else {
        auto &&result = co_await node;
        const uint32_t rank = preempt();
        if (rank < min_complete) {
            auto &any_tuple = meta_ptr->buffer[rank];
            std::get<0>(any_tuple) = idx;
            std::get<1>(any_tuple) = std::move(result);
            if (rank + 1 == min_complete) {
                if constexpr (is_thread_safe) {
                    std::atomic_thread_fence(std::memory_order_release);
                }
                detail::co_spawn_handle(meta_ptr->await_handle);
            }
        }
    }
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, typename... Ts>
task<detail::some_return_type<Ts...>>
some(uint32_t min_complete, task<Ts> &&...node) {
    constexpr uint32_t n = sizeof...(Ts);
    static_assert(n >= 2, "too few tasks for `some(...)`");
    assert(n >= min_complete && "too few tasks for `some(...)`");

    using meta_type = detail::some_meta<Ts...>;
    auto meta_ptr =
        std::make_shared<meta_type>(co_await lazy::who_am_i(), min_complete);

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(some_evaluate_to<is_thread_safe, idx, Ts...>(
                  min_complete, meta_ptr, std::move(node)
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

    co_return std::move(meta_ptr->buffer);
}

} // namespace co_context
