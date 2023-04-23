#pragma once

#include "co_context/detail/io_context_meta.hpp"
#include <concepts>

namespace co_context::detail {

template<typename T>
concept unlockable = requires(T mtx) {
    { mtx.unlock() } -> std::same_as<void>;
    noexcept(mtx.unlock());
};

/**
 * @brief lock_guard for coroutine.
 * @note Differ from std::lock_guard, the mutex must be held before
 * the construction of this lock_guard.
 */
template<typename mutex_t>
class [[nodiscard("Remember to hold the lock_guard.")]] lock_guard final {
    static_assert(unlockable<mutex_t>);

  public:
    explicit lock_guard(mutex_t &mtx) noexcept : mtx(mtx) {}

    ~lock_guard() noexcept { mtx.unlock(); }

    lock_guard(const lock_guard &) = delete;
#ifdef __INTELLISENSE__
    // clang-format off
    [[deprecated(
        "This function is for cheating intellisense, "
        "who doesn't sense RVO. "
        "You should NEVER use this explicitly or implicitly.")]]
    // clang-format on
    lock_guard(lock_guard &&other) noexcept
        : mtx(other.mtx) {
        assert(false && "Mandatory copy elision failed!");
    };
#else
    lock_guard(lock_guard &&other) = delete;
#endif

    lock_guard &operator=(const lock_guard &) = delete;
    lock_guard &operator=(lock_guard &&) = delete;

  private:
    mutex_t &mtx;
};

} // namespace co_context::detail
