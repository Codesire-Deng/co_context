#pragma once

#include "co_context/detail/trival_task.hpp"
#include <atomic>

namespace co_context {

class spin_mutex final {
  public:
    detail::trival_task lock() noexcept;

    bool try_lock() noexcept;

    void unlock() noexcept;

    explicit spin_mutex() noexcept = default;
    ~spin_mutex() noexcept = default;

    spin_mutex(const spin_mutex &) = delete;
    spin_mutex(spin_mutex &&) = delete;
    spin_mutex &operator=(const spin_mutex &) = delete;
    spin_mutex &operator=(spin_mutex &&) = delete;

  private:
    std::atomic_bool occupied{false};
};

} // namespace co_context