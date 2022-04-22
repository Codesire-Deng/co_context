#pragma once

#include <co_context/utility/bit.hpp>
#include <co_context/utility/as_atomic.hpp>

namespace co_context {

template<std::unsigned_integral T, T capacity>
struct spsc_cursor {
    static_assert(lowbit(capacity) == capacity);
    inline static constexpr T mask = capacity - 1;

    T m_head = 0;
    T m_tail = 0;

    inline T load_head() const noexcept {
        return as_c_atomic(m_head).load(std::memory_order_acquire) & mask;
    }

    inline T load_tail() const noexcept {
        return as_c_atomic(m_tail).load(std::memory_order_acquire) & mask;
    }

    inline T load_raw_head() const noexcept {
        return as_c_atomic(m_head).load(std::memory_order_acquire);
    }

    inline T load_raw_tail() const noexcept {
        return as_c_atomic(m_tail).load(std::memory_order_acquire);
    }

    inline T head() const noexcept { return m_head & mask; }

    inline T tail() const noexcept { return m_tail & mask; }

    inline T raw_head() const noexcept { return m_head; }

    inline T raw_tail() const noexcept { return m_tail; }

    // inline void set_raw_tail(T tail) const noexcept { m_tail = tail; }

    inline void store_raw_tail(T tail) noexcept {
        as_atomic(m_tail).store(tail, std::memory_order_release);
    }

    inline bool is_empty() const noexcept { return m_head == m_tail; }

    inline bool is_empty_load_head() const noexcept {
        return load_raw_head() == m_tail;
    }

    inline bool is_empty_load_tail() const noexcept {
        return m_head == load_raw_tail();
    }

    inline T available_number() const noexcept {
        return capacity - (m_tail - m_head);
    }

    inline bool is_available() const noexcept {
        return bool(capacity - (m_tail - m_head));
    }

    inline bool is_available_load_head() const noexcept {
        return bool(capacity - (m_tail - load_raw_head()));
    }

    inline void wait_for_available() const noexcept {
        while (load_raw_head() == m_tail - capacity)
            ;
        // as_c_atomic(m_head).wait(m_tail - capacity,
        // std::memory_order_acquire);
    }

    inline void wait_for_not_empty() const noexcept {
        while (load_raw_tail() == m_head)
            ;
        // as_c_atomic(m_tail).wait(m_head, std::memory_order_acquire);
    }

    inline void push(T num = 1) noexcept {
        as_atomic(m_tail).store(m_tail + num, std::memory_order_release);
    }

    inline void pop(T num = 1) noexcept {
        as_atomic(m_head).store(m_head + num, std::memory_order_release);
    }

    inline void push_notify(T num = 1) noexcept {
        as_atomic(m_tail).store(m_tail + num, std::memory_order_release);
        // as_c_atomic(m_tail).notify_one();
    }

    inline void pop_notify(T num = 1) noexcept {
        as_atomic(m_head).store(m_head + num, std::memory_order_release);
        // as_c_atomic(m_head).notify_one();
    }
};

} // namespace co_context
