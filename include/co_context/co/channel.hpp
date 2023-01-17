#pragma once

#include "co_context/co/condition_variable.hpp"
#include "co_context/task.hpp"
#include "co_context/utility/mpl.hpp"
#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>

namespace co_context {
template<std::move_constructible T, size_t capacity = 0>
class channel {
    static_assert(capacity != 0);

  public:
    [[nodiscard]] size_t size() const noexcept { return m_size; }

    [[nodiscard]] bool has_value() const noexcept { return !empty(); }

    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

    [[nodiscard]] bool full() const noexcept { return m_size == capacity; }

    task<> drop() {
        co_await m_mtx.lock();
        if (empty()) {
            co_await m_not_empty_cv.wait(m_mtx, [this] {
                return !this->empty();
            });
        }
        std::destroy_at(m_first);
        pop_one();
        m_mtx.unlock();
        m_not_full_cv.notify_one();
    }

    task<T> acquire() {
        co_await m_mtx.lock();
        if (empty()) {
            co_await m_not_empty_cv.wait(m_mtx, [this] {
                return !this->empty();
            });
        }
        T item{std::move(*m_first)};
        std::destroy_at(m_first);
        pop_one();
        m_mtx.unlock();
        m_not_full_cv.notify_one();
        co_return std::move(item);
    }

    template<typename... Args>
    task<> release(Args... args) {
        co_await m_mtx.lock();
        if (full()) {
            co_await m_not_full_cv.wait(m_mtx, [this] {
                return !this->full();
            });
        }
        std::construct_at(m_last, std::forward<Args>(args)...);
        push_one();
        m_mtx.unlock();
        m_not_empty_cv.notify_one();
    }

  private:
    std::array<mpl::uninitialized<T>, capacity> m_buf;
    T *m_first{reinterpret_cast<T *>(m_buf.data())};
    T *m_last{reinterpret_cast<T *>(m_buf.data())};
    size_t m_size{0};

    co_context::condition_variable m_not_full_cv;
    co_context::condition_variable m_not_empty_cv;
    co_context::mutex m_mtx;

    constexpr const T *buffer_start() const noexcept {
        return reinterpret_cast<const T *>(m_buf.data());
    }

    constexpr const T *buffer_end() const noexcept {
        return reinterpret_cast<const T *>(&(*m_buf.end()));
    }

    constexpr T *buffer_start() noexcept {
        return reinterpret_cast<T *>(m_buf.data());
    }

    constexpr T *buffer_end() noexcept {
        return reinterpret_cast<T *>(&(*m_buf.end()));
    }

    void pop_one() noexcept {
        ++m_first;
        --m_size;
        if (m_first == buffer_end()) [[unlikely]] {
            m_first = buffer_start();
        }
    }

    void push_one() noexcept {
        ++m_last;
        ++m_size;
        if (m_last == buffer_end()) [[unlikely]] {
            m_last = buffer_start();
        }
    }
};

template<std::move_constructible T>
class channel<T, 1UL> {
  public:
    [[nodiscard]] size_t size() const noexcept { return m_buf.has_value(); }

    [[nodiscard]] bool has_value() const noexcept { return m_buf.has_value(); }

    [[nodiscard]] bool empty() const noexcept { return !has_value(); }

    [[nodiscard]] bool full() const noexcept { return has_value(); }

    task<> drop() {
        co_await m_mtx.lock();
        if (this->empty()) {
            co_await m_not_empty_cv.wait(m_mtx, [this] {
                return this->has_value();
            });
        }
        m_buf = std::nullopt;
        m_mtx.unlock();
        m_not_full_cv.notify_one();
    }

    task<T> acquire() {
        co_await m_mtx.lock();
        if (this->empty()) {
            co_await m_not_empty_cv.wait(m_mtx, [this] {
                return this->has_value();
            });
        }
        T item{std::move(m_buf.value())};
        m_buf = std::nullopt;
        m_mtx.unlock();
        m_not_full_cv.notify_one();
        co_return std::move(item);
    }

    template<typename... Args>
    task<> release(Args... args) {
        co_await m_mtx.lock();
        if (this->full()) {
            co_await m_not_full_cv.wait(m_mtx, [this] {
                return !this->has_value();
            });
        }
        m_buf.emplace(std::forward<Args>(args)...);
        m_mtx.unlock();
        m_not_empty_cv.notify_one();
    }

  private:
    std::optional<T> m_buf{std::nullopt};
    co_context::condition_variable m_not_full_cv;
    co_context::condition_variable m_not_empty_cv;
    co_context::mutex m_mtx;
};

template<std::move_constructible T>
class channel<T, 0UL> {
  public:
    [[nodiscard]] consteval size_t size() const noexcept { return 0; }

    [[nodiscard]] bool has_value() const noexcept { return m_has_sender; }

    [[nodiscard]] bool empty() const noexcept { return m_receiver == nullptr; }

    [[nodiscard]] bool full() const noexcept { return has_value(); }

    task<> drop() {
        std::optional<T> item{std::nullopt};
        co_await m_acquire_mtx.lock();
        co_await m_match_mtx.lock();
        m_receiver = &item;
        if (m_has_sender) {
            m_match_cv.notify_one();
        }
        co_await m_match_cv.wait(m_match_mtx, [&item] {
            return item.has_value();
        });
        m_has_sender = false;
        m_match_mtx.unlock();
        m_acquire_mtx.unlock();
    }

    task<T> acquire() {
        std::optional<T> item{std::nullopt};
        co_await m_acquire_mtx.lock();
        co_await m_match_mtx.lock();
        m_receiver = &item;
        if (m_has_sender) {
            m_match_cv.notify_one();
        }
        co_await m_match_cv.wait(m_match_mtx, [&item] {
            return item.has_value();
        });
        m_has_sender = false;
        m_match_mtx.unlock();
        m_acquire_mtx.unlock();
        co_return std::move(item.value());
    }

    template<typename... Args>
    task<> release(Args... args) {
        co_await m_release_mtx.lock();
        co_await m_match_mtx.lock();
        // wait until the receiver exists
        if (m_receiver == nullptr) {
            m_has_sender = true;
            co_await m_match_cv.wait(m_match_mtx, [&receiver = m_receiver] {
                return receiver != nullptr;
            });
        }
        m_receiver->emplace(std::forward<Args>(args)...);
        m_receiver = nullptr;
        m_match_mtx.unlock();
        m_match_cv.notify_one();
        m_release_mtx.unlock();
    }

  private:
    co_context::mutex m_acquire_mtx;
    co_context::mutex m_release_mtx;
    co_context::condition_variable m_match_cv;
    co_context::mutex m_match_mtx;
    std::optional<T> *m_receiver{nullptr};
    bool m_has_sender = false;
};

} // namespace co_context
