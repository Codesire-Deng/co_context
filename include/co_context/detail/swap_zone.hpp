#pragma once

// #include <concepts>
#include "co_context/config.hpp"
#include "co_context/lockfree/spsc_cursor.hpp"
#include "co_context/utility/as_atomic.hpp"
#include "co_context/utility/bit.hpp"
#include <atomic>
#include <cstring>
#include <type_traits>

namespace co_context {

namespace detail {

    template<trival_ptr T>
    inline std::uintptr_t &as_uint(T &value) noexcept {
        return *reinterpret_cast<uintptr_t *>(std::addressof(value));
    }

    template<trival_ptr T>
    inline const std::uintptr_t &as_uint(const T &value) noexcept {
        return *reinterpret_cast<const uintptr_t *>(std::addressof(value));
    }

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct worker_swap_zone final {
        using sz_t = config::swap_capacity_size_t;
        using cur_t = ::co_context::spsc_cursor<sz_t, config::swap_capacity>;

        T data[config::swap_capacity];

        worker_swap_zone() noexcept = default;

        worker_swap_zone(const worker_swap_zone &) = delete;
        worker_swap_zone(worker_swap_zone &&) = delete;

        T operator[](sz_t i) const noexcept { return data[i]; }

        T &operator[](sz_t i) noexcept { return data[i]; }

        inline void push(cur_t &cur, T value) noexcept {
            data[cur.tail()] = value;
            cur.push(1);
        }

        inline void push_notify(cur_t &cur, T value) noexcept {
            data[cur.tail()] = value;
            cur.push_notify(1);
        }

        inline T front(const cur_t &cur) const noexcept {
            return data[cur.load_head()];
        }
    };

    template<typename T>
        requires std::is_trivially_copyable_v<T>
    struct swap_zone final {
        using sz_t = config::tid_t;

        worker_swap_zone<T> data[config::workers_number];

        swap_zone() noexcept = default;
        swap_zone(const swap_zone &) = delete;
        swap_zone(swap_zone &&) = delete;

        auto &operator[](sz_t i) noexcept { return data[i]; }

        const auto &operator[](sz_t i) const noexcept { return data[i]; }
    };

} // namespace detail

} // namespace co_context
