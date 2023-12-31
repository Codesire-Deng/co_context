#pragma once

#include <co_context/detail/tasklike.hpp>
#include <co_context/detail/uninitialize.hpp>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/mpl/type_list.hpp>
#include <co_context/utility/as_atomic.hpp>

#include <coroutine>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace co_context::detail {

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

template<mpl::TL out_type_list>
struct all_meta : all_meta_base {
    static_assert(mpl::count_v<out_type_list, void> == 0);

    using value_type = typename out_type_list::template to<std::tuple>;
    using buffer_type =
        typename mpl::map_t<out_type_list, detail::uninitialize>::template to<
            std::tuple>;

    buffer_type buffer;

    explicit all_meta(std::coroutine_handle<> await_handle, uint32_t n) noexcept
        : all_meta_base(await_handle, n) {}

    all_meta(const all_meta &) = delete;
    all_meta(all_meta &&) = delete;
    all_meta &operator=(const all_meta &) = delete;
    all_meta &operator=(all_meta &&) = delete;

    ~all_meta() noexcept(std::is_nothrow_destructible_v<value_type>) {
        std::destroy_at(&as_result());
    }

    value_type &as_result() & noexcept {
        return *reinterpret_cast<value_type *>(&buffer);
    }
};

template<>
struct all_meta<mpl::type_list<>> : all_meta_base {
    using all_meta_base::all_meta_base;
    using value_type = void;
};

template<tasklike... task_types>
struct all_trait {
  private:
    using in_type_list = mpl::type_list<typename task_types::value_type...>;

    using out_type_list = mpl::remove_t<in_type_list, void>;

  public:
    using meta_type = all_meta<out_type_list>;

    using value_type = typename meta_type::value_type;

    template<size_t idx>
    static constexpr size_t buffer_offset_v =
        idx - mpl::count_v<mpl::first_n_t<in_type_list, idx + 1>, void>;
};

template<
    safety is_thread_safe,
    typename all_meta_type,
    size_t buffer_offset,
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
        auto *const location = reinterpret_cast<node_value_type *>(
            std::get<buffer_offset>(meta.buffer).data
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

template<safety is_thread_safe = safety::safe, tasklike... task_types>
task<typename detail::all_trait<task_types...>::value_type>
all(task_types... node) {
    constexpr size_t n = sizeof...(task_types);
    static_assert(n >= 2, "too few tasks for `all(...)`");

    using trait = detail::all_trait<task_types...>;

    using all_meta_type = typename trait::meta_type;

    all_meta_type meta{co_await lazy::who_am_i(), n};

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (..., co_spawn(detail::all_evaluate_to<
                       is_thread_safe, all_meta_type,
                       trait::template buffer_offset_v<idx>, task_types>(
                  meta, std::move(node)
              )));
    };

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_release);
    }

    spawn_all(std::index_sequence_for<task_types...>{});

    co_await lazy::forget();

    if constexpr (is_thread_safe) {
        std::atomic_thread_fence(std::memory_order_acquire);
    }

    if constexpr (std::is_void_v<typename trait::value_type>) {
        co_return;
    } else {
        co_return std::move(meta.as_result());
    }
}

} // namespace co_context
