#include "co_context/co/spin_mutex.hpp"
#include "co_context/lazy_io.hpp"

namespace co_context {

detail::trival_task spin_mutex::lock() noexcept {
    for (;;) {
        if (!occupied.exchange(true, std::memory_order_acquire)) {
            co_return;
        }
        while (occupied.load(std::memory_order_relaxed)) {
            co_await lazy::yield();
        }
    }
}

bool spin_mutex::try_lock() noexcept {
    return !occupied.load(std::memory_order_relaxed)
           && !occupied.exchange(true, std::memory_order_acquire);
}

void spin_mutex::unlock() noexcept {
    occupied.store(false, std::memory_order_release);
}

} // namespace co_context
