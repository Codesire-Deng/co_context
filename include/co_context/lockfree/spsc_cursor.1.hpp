#pragma once

#include <co_context/utility/bit.hpp>
#include <co_context/utility/as_atomic.hpp>

namespace co_context {

template<std::unsigned_integral T, T capacity>
struct spsc_cursor {
    static_assert(lowbit(capacity) == capacity);
    inline static constexpr T mask = capacity - 1;

    std::atomic<T> m_head = 0;
    std::atomic<T> m_tail = 0;

    inline bool is_empty() const noexcept {
        return m_head.load(std::memory_order_relaxed)
               == m_tail.load(std::memory_order_relaxed);
    }

    inline bool is_empty_load_head() const noexcept {
        return m_head.load(std::memory_order_acquire)
               == m_tail.load(std::memory_order_relaxed);
    }

    inline bool is_empty_load_tail() const noexcept {
        return m_head.load(std::memory_order_relaxed)
               == m_tail.load(std::memory_order_acquire);
    }

    inline T size() const noexcept {
        return m_tail.load(std::memory_order_relaxed)
               - m_head.load(std::memory_order_relaxed);
    }

    inline T available_number() const noexcept {
        return capacity
               - (m_tail.load(std::memory_order_relaxed)
                  - m_head.load(std::memory_order_relaxed));
    }

    inline bool is_available() const noexcept {
        return bool(
            capacity
            - (m_tail.load(std::memory_order_relaxed)
               - m_head.load(std::memory_order_relaxed))
        );
    }

    inline void push(T num = 1) noexcept {
        // as_atomic(m_tail).store(m_tail + num, std::memory_order_release);
        m_tail.store(
            m_tail.load(std::memory_order_relaxed) + num,
            std::memory_order_release
        );
    }

    inline void pop(T num = 1) noexcept {
        // as_atomic(m_head).store(m_head + num, std::memory_order_release);
        m_head.store(
            m_head.load(std::memory_order_relaxed) + num,
            std::memory_order_release
        );
    }

    inline T load_head() const noexcept {
        // return as_c_atomic(m_head).load(std::memory_order_acquire) & mask;
        return m_head.load(std::memory_order_acquire) & mask;
    }

    inline T load_tail() const noexcept {
        // return as_c_atomic(m_tail).load(std::memory_order_acquire) & mask;
        return m_tail.load(std::memory_order_acquire) & mask;
    }

    inline T head() const noexcept {
        return m_head.load(std::memory_order_relaxed) & mask;
    }

    inline T tail() const noexcept {
        return m_tail.load(std::memory_order_relaxed) & mask;
    }

    inline T raw_head() const noexcept {
        return m_head.load(std::memory_order_relaxed);
    }

    inline T raw_tail() const noexcept {
        return m_tail.load(std::memory_order_relaxed);
    }

    inline T load_raw_tail() const noexcept {
        return m_tail.load(std::memory_order_acquire);
    }
};

} // namespace co_context
