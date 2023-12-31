#pragma once

#include <co_context/config/io_context.hpp>
#include <co_context/detail/compat.hpp>

#include <atomic>

namespace co_context::detail {

class spinlock final {
  private:
    std::atomic_bool occupied{false};

  public:
    void lock() noexcept;

    bool try_lock() noexcept;

    void unlock() noexcept;

    explicit spinlock() noexcept = default;
    ~spinlock() noexcept = default;

    spinlock(const spinlock &) = delete;
    spinlock(spinlock &&) = delete;
    spinlock &operator=(const spinlock &) = delete;
    spinlock &operator=(spinlock &&) = delete;
};

inline void spinlock::lock() noexcept {
    for (;;) {
        if (!occupied.exchange(true, std::memory_order_acquire)) {
            return;
        }
        while (occupied.load(std::memory_order_relaxed)) {
            if constexpr (config::is_using_hyper_threading) {
                CO_CONTEXT_PAUSE();
            }
        }
    }
}

inline bool spinlock::try_lock() noexcept {
    return !occupied.load(std::memory_order_relaxed)
           && !occupied.exchange(true, std::memory_order_acquire);
}

inline void spinlock::unlock() noexcept {
    occupied.store(false, std::memory_order_release);
}

} // namespace co_context::detail
