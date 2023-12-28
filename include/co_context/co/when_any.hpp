#pragma once

#include <co_context/detail/tasklike.hpp>
#include <co_context/io_context.hpp>
#include <co_context/lazy_io.hpp>
#include <co_context/mpl/type_list.hpp>
#include <co_context/utility/as_atomic.hpp>

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace co_context {

template<typename T>
struct index_value {
    uint32_t index;
    T value;
};

} // namespace co_context

namespace co_context::detail {

struct any_meta_base {
    std::coroutine_handle<> await_handle;

    // NOTE NOT thread-safe!  If `resume_on` is used, race condition may
    // happen!
    uint32_t idx{-1U};
    uint32_t finish_count{0};

    explicit any_meta_base(std::coroutine_handle<> await_handle) noexcept
        : await_handle(await_handle) {}

    template<bool is_thread_safe>
    [[nodiscard]]
    bool is_cancelled() const noexcept {
        if constexpr (is_thread_safe) {
            return as_c_atomic(this->finish_count)
                       .load(std::memory_order_acquire)
                   != 0;
        } else {
            return this->finish_count != 0;
        }
    }

    template<bool is_thread_safe>
    [[nodiscard]]
    bool preempt() noexcept {
        if constexpr (is_thread_safe) {
            return as_atomic(this->finish_count)
                       .fetch_add(1, std::memory_order_acquire)
                   == 0;
        } else {
            return this->finish_count++ == 0;
        }
    }

    template<bool is_thread_safe>
    void co_spawn() const noexcept {
        if constexpr (is_thread_safe) {
            std::atomic_thread_fence(std::memory_order_release);
        }
        detail::co_spawn_handle(this->await_handle);
    }
};

template<typename Variant>
struct any_meta : any_meta_base {
    Variant buffer;

    using any_meta_base::any_meta_base;

    Variant &as_result() & noexcept { return buffer; }
};

template<>
struct any_meta<uint32_t> : any_meta_base {};

template<tasklike... task_types>
struct any_trait {
  private:
    using in_type_list = mpl::type_list<typename task_types::value_type...>;

    using out_type_list =
        typename mpl::remove_t<in_type_list, void>::template prepend<
            std::monostate>;

    using variant_type = typename out_type_list::template to<std::variant>;

  public:
    static constexpr bool is_all_void =
        (mpl::count_v<in_type_list, void> == sizeof...(task_types));

    using index_type = uint32_t;

    using value_type =
        std::conditional_t<is_all_void, index_type, index_value<variant_type>>;

    using meta_type = any_meta<variant_type>;
};

template<
    safety is_thread_safe,
    typename any_meta_type,
    size_t idx,
    tasklike task_type>
task<void> any_evaluate_to(
    std::shared_ptr<any_meta_type> meta_ptr,
    task_type node // take the ownership
) {
    using node_return_type = typename task_type::value_type;

    if (meta_ptr->template is_cancelled<is_thread_safe>()) {
        co_return;
    }

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
        if (meta_ptr->template preempt<is_thread_safe>()) {
            meta_ptr->idx = idx;
            meta_ptr->template co_spawn<is_thread_safe>();
        }
    } else {
        auto &&result = co_await node;
        if (meta_ptr->template preempt<is_thread_safe>()) {
            if constexpr (requires { typename task_type::is_shared_task; }) {
                meta_ptr->buffer = result;
            } else {
                meta_ptr->buffer = std::move(result);
            }
            meta_ptr->idx = idx;
            meta_ptr->template co_spawn<is_thread_safe>();
        }
    }
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, tasklike... task_types>
task<typename detail::any_trait<task_types...>::value_type>
any(task_types... node) {
    constexpr uint32_t n = sizeof...(task_types);
    static_assert(n >= 2, "too few tasks for `any(...)`");

    using trait = detail::any_trait<task_types...>;
    using meta_type = typename trait::meta_type;
    auto meta_ptr = std::make_shared<meta_type>(co_await lazy::who_am_i());

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (...,
         co_spawn(any_evaluate_to<is_thread_safe, meta_type, idx, task_types>(
             meta_ptr, std::move(node)
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

    if constexpr (trait::is_all_void) {
        co_return meta_ptr->idx;
    } else {
        using value_type = typename trait::value_type;
        co_return value_type{meta_ptr->idx, std::move(meta_ptr->buffer)};
    }
}
} // namespace co_context

namespace co_context::detail {

template<typename Vector>
struct some_meta {
    Vector buffer;
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

    Vector &as_result() & noexcept { return buffer; }

    template<bool is_thread_safe>
    [[nodiscard]]
    bool is_cancelled(uint32_t min_complete) const noexcept {
        if constexpr (is_thread_safe) {
            return as_c_atomic(this->idx).load(std::memory_order_acquire)
                   >= min_complete;
        } else {
            return this->idx >= min_complete;
        }
    }

    template<bool is_thread_safe>
    [[nodiscard]]
    uint32_t preempt() noexcept {
        if constexpr (is_thread_safe) {
            return as_atomic(this->idx).fetch_add(1, std::memory_order_acquire);
        } else {
            return this->idx++;
        }
    }

    template<bool is_thread_safe>
    void co_spawn() const noexcept {
        if constexpr (is_thread_safe) {
            std::atomic_thread_fence(std::memory_order_release);
        }
        detail::co_spawn_handle(this->await_handle);
    }
};

template<tasklike... task_types>
struct some_trait {
  private:
    using in_type_list = mpl::type_list<typename task_types::value_type...>;

    using element_type = typename any_trait<task_types...>::value_type;

  public:
    using value_type = std::vector<element_type>;

    using meta_type = some_meta<value_type>;
};

template<
    safety is_thread_safe,
    typename some_meta_type,
    size_t idx,
    tasklike task_type>
task<void> some_evaluate_to(
    const uint32_t min_complete,
    std::shared_ptr<some_meta_type> meta_ptr,
    task_type node // take the ownership
) {
    using node_return_type = typename task_type::value_type;

    if (meta_ptr->template is_cancelled<is_thread_safe>(min_complete)) {
        co_return;
    }

    if constexpr (std::is_void_v<node_return_type>) {
        co_await node;
        const uint32_t rank = meta_ptr->template preempt<is_thread_safe>();
        if (rank < min_complete) {
            meta_ptr->buffer[rank].index = idx;
            if (rank + 1 == min_complete) {
                meta_ptr->template co_spawn<is_thread_safe>();
            }
        }
    } else {
        auto &&result = co_await node;
        const uint32_t rank = meta_ptr->template preempt<is_thread_safe>();
        if (rank < min_complete) {
            auto &any_tuple = meta_ptr->buffer[rank];
            any_tuple.index = idx;
            any_tuple.value = std::move(result);
            if (rank + 1 == min_complete) {
                meta_ptr->template co_spawn<is_thread_safe>();
            }
        }
    }
}

} // namespace co_context::detail

namespace co_context {

template<safety is_thread_safe = safety::safe, tasklike... task_types>
task<typename detail::some_trait<task_types...>::value_type>
some(uint32_t min_complete, task_types... node) {
    constexpr uint32_t n = sizeof...(task_types);
    static_assert(n >= 2, "too few tasks for `some(...)`");
    assert(n >= min_complete && "too few tasks for `some(...)`");
    assert(min_complete >= 1 && "min_complete should be at least 1");

    using trait = detail::some_trait<task_types...>;
    using meta_type = typename trait::meta_type;
    auto meta_ptr =
        std::make_shared<meta_type>(co_await lazy::who_am_i(), min_complete);

    auto spawn_all = [&]<size_t... idx>(std::index_sequence<idx...>) {
        (...,
         co_spawn(some_evaluate_to<is_thread_safe, meta_type, idx, task_types>(
             min_complete, meta_ptr, std::move(node)
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

    co_return std::move(meta_ptr->buffer);
}

} // namespace co_context
