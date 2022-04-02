#pragma once

// #include <concepts>
#include <type_traits>
#include <atomic>
#include "co_context/config.hpp"

namespace co_context {

namespace detail {

    template<typename T>
    concept swap_zone_content = std::is_trivially_copyable_v<T> && sizeof(T)
                                == sizeof(std::uintptr_t);

    struct worker_swap_cur final {
        using T = config::worker_swap_zone_width_t;
        T off = 0;

        void next() noexcept {
            off = (off + 1) % co_context::config::swap_capacity;
        }
    };

    template<swap_zone_content T>
    struct worker_swap_zone final {
        using sz_t = config::worker_swap_zone_width_t;

        T data[config::swap_capacity];

        worker_swap_zone() noexcept { memset(data, 0, sizeof(data)); }
        worker_swap_zone(const worker_swap_zone &) = delete;
        worker_swap_zone(worker_swap_zone &&) = delete;

        T &operator[](worker_swap_cur cur) noexcept { return data[cur.off]; }

        const T &operator[](worker_swap_cur cur) const noexcept {
            return data[cur.off];
        }

        bool try_find_empty(worker_swap_cur &cur) const noexcept {
            const std::uintptr_t value =
                *reinterpret_cast<const std::uintptr_t *>(
                    std::addressof(data[cur.off]));
            sz_t i = 0;
            while (i < config::swap_capacity && value != 0) {
                ++i;
                cur.next();
            }
            if (i == config::swap_capacity) [[unlikely]] {
                cur.next();
                return false;
            }
            return true;
        }

        bool try_find_exist(worker_swap_cur &cur) const noexcept {
            const std::uintptr_t value =
                *reinterpret_cast<const std::uintptr_t *>(
                    std::addressof(data[cur.off]));
            sz_t i = 0;
            while (i < config::swap_capacity && value == 0) {
                ++i;
                cur.next();
            }
            if (i == config::swap_capacity) [[unlikely]] {
                cur.next();
                return false;
            }
            return true;
        }

        T load(
            const worker_swap_cur cur, std::memory_order order) const noexcept {
            return std::atomic_load_explicit(
                reinterpret_cast<const std::atomic<T> *>(
                    std::addressof((*this)[cur])),
                order);
        }

        void store(
            const worker_swap_cur cur,
            T value,
            std::memory_order order) noexcept {
            std::atomic_store_explicit(
                reinterpret_cast<std::atomic<T> *>(
                    std::addressof((*this)[cur])),
                value, order);
        }
    };

    struct context_swap_cur final {
        using T = config::worker_swap_zone_width_t;
        T off = 0;

        using U = config::thread_zone_width_t;
        U tid = 0;

        void next() noexcept {
            if (++tid == config::worker_threads_number) [[unlikely]] {
                tid = 0;
                off = (off + 1) % config::swap_capacity;
            }
        }
    };

    template<swap_zone_content T>
    struct swap_zone final {
        using sz_t = config::worker_swap_zone_width_t;

        static constexpr uint32_t swap_length =
            config::worker_threads_number * config::swap_capacity;

        worker_swap_zone<T> data[config::worker_threads_number];

        swap_zone() noexcept = default;
        swap_zone(const swap_zone &) = delete;
        swap_zone(swap_zone &&) = delete;

        auto &operator[](sz_t i) noexcept { return data[i]; }

        const auto &operator[](sz_t i) const noexcept { return data[i]; }

        T &operator[](context_swap_cur cur) noexcept {
            return data[cur.tid].data[cur.off];
        }

        const T &operator[](context_swap_cur cur) const noexcept {
            return data[cur.tid].data[cur.off];
        }

        bool try_find_empty(context_swap_cur &cur) const noexcept {
            const std::uintptr_t value =
                *reinterpret_cast<const std::uintptr_t *>(
                    std::addressof((*this)[cur]));
            uint32_t i = 0;

            while (i < swap_length && value != 0) {
                ++i;
                cur.next();
            }
            if (i != swap_length) [[likely]] {
                return true;
            } else {
                cur.next();
                return false;
            }
        }

        bool try_find_exist(context_swap_cur &cur) const noexcept {
            sz_t i = 0;
            while (i < swap_length
                   && reinterpret_cast<std::uintptr_t>((*this)[cur]) == 0) {
                ++i;
                cur.next();
            }
            if (i == swap_length) [[unlikely]] {
                cur.next();
                return false;
            }
            return true;
        }

        T load(const context_swap_cur cur, std::memory_order order)
            const noexcept {
            return std::atomic_load_explicit(
                reinterpret_cast<const std::atomic<T> *>(
                    std::addressof((*this)[cur])),
                order);
        }

        void store(
            const context_swap_cur cur,
            T value,
            std::memory_order order) noexcept {
            std::atomic_store_explicit(
                reinterpret_cast<std::atomic<T> *>(
                    std::addressof((*this)[cur])),
                value, order);
        }
    };

} // namespace detail

} // namespace co_context
