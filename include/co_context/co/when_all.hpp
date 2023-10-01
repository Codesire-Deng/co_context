#pragma once

#include <co_context/config.hpp>
#include <co_context/detail/tasklike.hpp>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/shared_task.hpp>
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

struct all_meta_base {
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t wait_num;

    explicit
    all_meta_base(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : await_handle(await_handle)
        , wait_num(n) {}

    template<safety is_thread_safe>
    void count_down() noexcept {
        bool need_wakeup;
        if constexpr (is_thread_safe) {
            need_wakeup =
                (as_atomic(wait_num).fetch_sub(1, std::memory_order_relaxed)
                 == 1);
        } else {
            need_wakeup = (--wait_num == 0);
        }
        if (need_wakeup) {
            if constexpr (is_thread_safe) {
                std::atomic_thread_fence(std::memory_order_release);
            }
            detail::co_spawn_handle(await_handle);
        }
    }
};

template<typename... Ts>
struct all_meta : all_meta_base {
    using result_type = std::tuple<Ts...>;
    using buffer_type = std::tuple<mpl::uninitialized<Ts>...>;
    using result_type_list = mpl::type_list<Ts...>;

    static_assert(sizeof...(Ts) != 0);
    static_assert(mpl::count_v<result_type_list, void> == 0);

    buffer_type buffer;

    explicit all_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : all_meta_base(await_handle, n) {}

    all_meta(const all_meta &) = delete;
    all_meta(all_meta &&) = delete;
    all_meta &operator=(const all_meta &) = delete;
    all_meta &operator=(all_meta &&) = delete;

    ~all_meta() noexcept(noexcept(std::destroy_at(&as_result()))) {
        std::destroy_at(&as_result());
    }

    result_type &as_result() & noexcept {
        return *reinterpret_cast<result_type *>(&buffer);
    }
};

template<>
struct all_meta<> : all_meta_base {
    using all_meta_base::all_meta_base;
};

template<typename... Ts>
using to_all_meta_t = typename clear_void_t<Ts...>::template to<all_meta>;

template<size_t idx, typename... Ts>
struct get_buffer_offset {
  private:
    static constexpr bool is_void_v =
        std::is_same_v<void, mpl::select<mpl::type_list<Ts...>, idx>>;
    using list = mpl::first_N_t<mpl::type_list<Ts...>, idx + 1>;

  public:
    static constexpr size_t value =
        is_void_v ? -1 : (idx - mpl::count_v<list, void>);
};

template<size_t idx, typename... Ts>
inline constexpr size_t get_buffer_offset_v =
    get_buffer_offset<idx, Ts...>::value;

template<
    safety is_thread_safe,
    typename all_meta_type,
    size_t pos,
    tasklike task_type>
    requires std::is_base_of_v<all_meta_base, all_meta_type>
task<void> all_evaluate_to(all_meta_type &meta, task_type &&node) {
    using node_value_type = typename task_type::value_type;

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    if constexpr (std::is_void_v<node_value_type>) {
        co_await node;
    } else {
        auto *const location =
            reinterpret_cast<node_value_type *>(std::get<pos>(meta.buffer).data
            );

        if constexpr (requires { typename task_type::is_shared_task; }) {
            std::construct_at(location, co_await std::forward<task_type>(node));
        } else {
            std::construct_at(
                location, std::move(co_await std::forward<task_type>(node))
            );
        }
    }

    meta.template count_down<is_thread_safe>();
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, tasklike... tasklikes>
task<detail::tuple_or_void<typename tasklikes::value_type...>>
all(tasklikes... node) {
    constexpr size_t n = sizeof...(tasklikes);
    static_assert(n >= 2, "too few tasks for `all(...)`");

    using all_meta_type =
        detail::to_all_meta_t<typename tasklikes::value_type...>;
    all_meta_type meta{co_await lazy::who_am_i(), n};

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(detail::all_evaluate_to<
                       is_thread_safe, all_meta_type,
                       detail::get_buffer_offset_v<
                           idx, typename tasklikes::value_type...>,
                       tasklikes>(meta, std::move(node))));
    };

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_release);
    }

    spawn_all(std::index_sequence_for<tasklikes...>{});

    co_await lazy::forget();

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    if constexpr (std::is_void_v<detail::tuple_or_void<
                      typename tasklikes::value_type...>>) {
        co_return;
    } else {
        co_return std::move(meta.as_result());
    }
}

} // namespace co_context
