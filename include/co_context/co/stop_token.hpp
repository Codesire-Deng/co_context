#pragma once

#if __has_include(<stop_token>)

#include <stop_token>

namespace co_context {

using nostopstate_t = std::nostopstate_t;
using std::nostopstate;

using stop_source = std::stop_source;

using stop_token = std::stop_token;

// Language feature required: CTAD for aggregates and aliases
#if __cpp_deduction_guides >= 201907L

template<typename Callback>
using stop_callback = std::stop_callback<Callback>;

#else

template<typename Callback>
class stop_callback : public std::stop_callback<Callback> {
    using base = std::stop_callback<Callback>;
    using base::base;
};

template<typename Callback>
stop_callback(stop_token, Callback) -> stop_callback<Callback>;

#endif

} // namespace co_context

#else

#include "co_context/detail/compat.hpp"
#include <atomic>
#include <concepts>
#include <semaphore>
#include <thread>

namespace co_context {

// Tag type indicating a stop_source should have no shared-stop-state.
struct nostopstate_t {
    explicit nostopstate_t() = default;
};

inline constexpr nostopstate_t nostopstate{};

class stop_source;

// Allow testing whether a stop request has been made on a `stop_source`.
class stop_token {
  public:
    stop_token() noexcept = default;

    stop_token(const stop_token &) noexcept = default;
    stop_token(stop_token &&) noexcept = default;

    ~stop_token() noexcept = default;

    stop_token &operator=(const stop_token &) noexcept = default;

    stop_token &operator=(stop_token &&) noexcept = default;

    [[nodiscard]]
    bool stop_possible() const noexcept {
        return static_cast<bool>(m_state) && m_state->stop_possible();
    }

    [[nodiscard]]
    bool stop_requested() const noexcept {
        return static_cast<bool>(m_state) && m_state->stop_requested();
    }

    void swap(stop_token &rhs) noexcept { m_state.swap(rhs.m_state); }

    [[nodiscard]]
    friend bool
    operator==(const stop_token &lhs, const stop_token &rhs) {
        return lhs.m_state == rhs.m_state;
    }

    friend void swap(stop_token &lhs, stop_token &rhs) noexcept {
        lhs.swap(rhs);
    }

  private:
    friend class stop_source;
    template<typename Callback>
    friend class stop_callback;

    static void do_yield() noexcept {
        CO_CONTEXT_PAUSE();
        std::this_thread::yield();
    }

    struct stop_cb {
        using cb_type = void(stop_cb *) noexcept;
        cb_type *m_callback;
        stop_cb *m_prev = nullptr;
        stop_cb *m_next = nullptr;
        bool *m_destroyed = nullptr;
        std::binary_semaphore m_done{0};

        [[__gnu__::__nonnull__]] explicit stop_cb(cb_type *cb)
            : m_callback(cb) {}

        void run() noexcept { m_callback(this); }
    };

    struct stop_state_t {
        using value_type = uint32_t;
        static constexpr value_type stop_requested_bit = 1;
        static constexpr value_type locked_bit = 2;
        static constexpr value_type ssrc_counter_inc = 4;

        std::atomic<value_type> m_owners{1};
        std::atomic<value_type> m_value{ssrc_counter_inc};
        stop_cb *m_head = nullptr;
        std::thread::id m_requester;

        stop_state_t() noexcept = default;

        bool stop_possible() noexcept {
            // true if a stop request has already been made or there are still
            // stop_source objects that would allow one to be made.
            return m_value.load(std::memory_order::acquire) & ~locked_bit;
        }

        bool stop_requested() noexcept {
            return m_value.load(std::memory_order::acquire)
                   & stop_requested_bit;
        }

        void add_owner() noexcept {
            m_owners.fetch_add(1, std::memory_order::relaxed);
        }

        void release_ownership() noexcept {
            if (m_owners.fetch_sub(1, std::memory_order::acq_rel) == 1) {
                delete this;
            }
        }

        void add_ssrc() noexcept {
            m_value.fetch_add(ssrc_counter_inc, std::memory_order::relaxed);
        }

        void sub_ssrc() noexcept {
            m_value.fetch_sub(ssrc_counter_inc, std::memory_order::release);
        }

        // Obtain lock.
        void lock() noexcept {
            // Can use relaxed loads to get the current value.
            // The successful call to m_try_lock is an acquire operation.
            auto old = m_value.load(std::memory_order::relaxed);
            while (!try_lock(old, std::memory_order::relaxed)) {}
        }

        // Precondition: calling thread holds the lock.
        void unlock() noexcept {
            m_value.fetch_sub(locked_bit, std::memory_order::release);
        }

        bool request_stop() noexcept {
            // obtain lock and set stop_requested bit
            auto old = m_value.load(std::memory_order::acquire);
            do {
                if (old & stop_requested_bit) { // stop request already made
                    return false;
                }
            } while (!try_lock_and_stop(old));

            m_requester = std::this_thread::get_id();

            while (m_head) {
                bool last_cb;
                stop_cb *cb = m_head;
                m_head = m_head->m_next;
                if (m_head) {
                    m_head->m_prev = nullptr;
                    last_cb = false;
                } else {
                    last_cb = true;
                }

                // Allow other callbacks to be unregistered while cb runs.
                unlock();

                bool destroyed = false;
                cb->m_destroyed = &destroyed;

                // run Callback
                cb->run();

                if (!destroyed) {
                    cb->m_destroyed = nullptr;

                    // synchronize with destructor of stop_callback that owns
                    // *cb
                    if (!CO_CONTEXT_IS_SINGLE_THREADED) {
                        cb->m_done.release();
                    }
                }

                // Avoid relocking if we already know there are no more
                // callbacks.
                if (last_cb) {
                    return true;
                }

                lock();
            }

            unlock();
            return true;
        }

        [[__gnu__::__nonnull__]]
        bool register_callback(stop_cb *cb) noexcept {
            auto old = m_value.load(std::memory_order::acquire);
            do {
                if (old & stop_requested_bit) // stop request already made
                {
                    cb->run();                // run synchronously
                    return false;
                }

                if (old < ssrc_counter_inc) { // no stop_source owns *this
                    // No need to register Callback if no stop request can be
                    // made. Returning false also means the stop_callback does
                    // not share ownership of this state, but that's not
                    // observable.
                    return false;
                }
            } while (!try_lock(old));

            cb->m_next = m_head;
            if (m_head) {
                m_head->m_prev = cb;
            }
            m_head = cb;
            unlock();
            return true;
        }

        // Called by ~stop_callback just before destroying *cb.
        [[__gnu__::__nonnull__]]
        void remove_callback(stop_cb *cb) noexcept {
            lock();

            if (cb == m_head) {
                m_head = m_head->m_next;
                if (m_head) {
                    m_head->m_prev = nullptr;
                }
                unlock();
                return;
            }
            if (cb->m_prev) {
                cb->m_prev->m_next = cb->m_next;
                if (cb->m_next) {
                    cb->m_next->m_prev = cb->m_prev;
                }
                unlock();
                return;
            }

            unlock();

            // Callback is not in the list, so must have been removed by a call
            // to m_request_stop.

            // Despite appearances there is no data race on m_requester. The
            // only write to it happens before the Callback is removed from the
            // list, and removing it from the list happens before this read.
            if (!(m_requester == std::this_thread::get_id())) {
                // Synchronize with completion of Callback.
                cb->m_done.acquire();
                // Safe for ~stop_callback to destroy *cb now.
                return;
            }

            if (cb->m_destroyed) {
                *cb->m_destroyed = true;
            }
        }

        // Try to obtain the lock.
        // Returns true if the lock is acquired (with memory order acquire).
        // Otherwise, sets __curval = m_value.load(__failure) and returns
        // false. Might fail spuriously, so must be called in a loop.
        bool try_lock(
            value_type &curval,
            std::memory_order failure = std::memory_order::acquire
        ) noexcept {
            return do_try_lock(curval, 0, std::memory_order::acquire, failure);
        }

        // Try to obtain the lock to make a stop request.
        // Returns true if the lock is acquired and the _S_stop_requested_bit is
        // set (with memory order acq_rel so that other threads see the
        // request). Otherwise, sets __curval =
        // m_value.load(std::memory_order::acquire) and returns false. Might
        // fail spuriously, so must be called in a loop.
        bool try_lock_and_stop(value_type &curval) noexcept {
            return do_try_lock(
                curval, stop_requested_bit, std::memory_order::acq_rel,
                std::memory_order::acquire
            );
        }

        bool do_try_lock(
            value_type &curval,
            value_type newbits,
            std::memory_order success,
            std::memory_order failure
        ) noexcept {
            if (curval & locked_bit) {
                do_yield();
                curval = m_value.load(failure);
                return false;
            }
            newbits |= locked_bit;
            return m_value.compare_exchange_weak(
                curval, curval | newbits, success, failure
            );
        }
    };

    struct stop_state_ref {
        stop_state_ref() = default;

        explicit stop_state_ref(const stop_source & /*unused*/)
            : m_ptr(new stop_state_t()) {}

        stop_state_ref(const stop_state_ref &other) noexcept
            : m_ptr(other.m_ptr) {
            if (m_ptr) {
                m_ptr->add_owner();
            }
        }

        stop_state_ref(stop_state_ref &&other) noexcept : m_ptr(other.m_ptr) {
            other.m_ptr = nullptr;
        }

        stop_state_ref &operator= /*NOLINT*/ (const stop_state_ref &other
        ) noexcept {
            if (auto *ptr = other.m_ptr; ptr != m_ptr) {
                if (ptr) {
                    ptr->add_owner();
                }
                if (m_ptr) {
                    m_ptr->release_ownership();
                }
                m_ptr = ptr;
            }
            return *this;
        }

        stop_state_ref &operator=(stop_state_ref &&other) noexcept {
            stop_state_ref(std::move(other)).swap(*this);
            return *this;
        }

        ~stop_state_ref() noexcept {
            if (m_ptr) {
                m_ptr->release_ownership();
            }
        }

        void swap(stop_state_ref &other) noexcept {
            std::swap(m_ptr, other.m_ptr);
        }

        explicit operator bool() const noexcept { return m_ptr != nullptr; }

        stop_state_t *operator->() const noexcept { return m_ptr; }

#if __cpp_impl_three_way_comparison >= 201907L
        friend bool
        operator==(const stop_state_ref &, const stop_state_ref &) = default;
#else
        friend bool operator==(
            const stop_state_ref &lhs, const stop_state_ref &rhs
        ) noexcept {
            return lhs.m_ptr == rhs.m_ptr;
        }

        friend bool operator!=(
            const stop_state_ref &lhs, const stop_state_ref &rhs
        ) noexcept {
            return lhs.m_ptr != rhs.m_ptr;
        }
#endif

      private:
        stop_state_t *m_ptr = nullptr;
    };

    stop_state_ref m_state;

    explicit stop_token(const stop_state_ref &state) noexcept
        : m_state{state} {}
};

/// A type that allows a stop request to be made.
class stop_source {
  public:
    stop_source() : m_state(*this) {}

    explicit stop_source(nostopstate_t /*unused*/) noexcept {}

    stop_source(const stop_source &other) noexcept : m_state(other.m_state) {
        if (m_state) {
            m_state->add_ssrc();
        }
    }

    stop_source(stop_source &&) noexcept = default;

    stop_source &operator=(const stop_source &other) noexcept {
        if (m_state != other.m_state) {
            stop_source sink(std::move(*this));
            m_state = other.m_state;
            if (m_state) {
                m_state->add_ssrc();
            }
        }
        return *this;
    }

    stop_source &operator=(stop_source &&) noexcept = default;

    ~stop_source() noexcept {
        if (m_state) {
            m_state->sub_ssrc();
        }
    }

    [[nodiscard]]
    bool stop_possible() const noexcept {
        return static_cast<bool>(m_state);
    }

    [[nodiscard]]
    bool stop_requested() const noexcept {
        return static_cast<bool>(m_state) && m_state->stop_requested();
    }

    bool request_stop() noexcept {
        if (stop_possible()) {
            return m_state->request_stop();
        }
        return false;
    }

    [[nodiscard]]
    stop_token get_token() const noexcept {
        return stop_token{m_state};
    }

    void swap(stop_source &other) noexcept { m_state.swap(other.m_state); }

    [[nodiscard]]
    friend bool
    operator==(const stop_source &lhs, const stop_source &rhs) noexcept {
        return lhs.m_state == rhs.m_state;
    }

    friend void swap(stop_source &lhs, stop_source &rhs) noexcept {
        lhs.swap(rhs);
    }

  private:
    stop_token::stop_state_ref m_state;
};

/// A wrapper for callbacks to be run when a stop request is made.
template<typename Callback>
class [[nodiscard]] stop_callback {
    static_assert(std::is_nothrow_destructible_v<Callback>);
    static_assert(std::is_invocable_v<Callback>);

  public:
    using callback_type = Callback;

    template<typename Cb
             // ,enable_if_t<std::is_constructible_v<Callback, Cb>, int> = 0
             >
        requires std::constructible_from<Callback, Cb>
    explicit stop_callback(
        const stop_token &token, Cb &&cb
    ) noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
        : m_cb(std::forward<Cb>(cb)) {
        if (auto state = token.m_state) {
            if (state->register_callback(&m_cb)) {
                m_state.swap(state);
            }
        }
    }

    template<typename Cb
             // ,enable_if_t<std::is_constructible_v<Callback, Cb>, int> = 0
             >
        requires std::constructible_from<Callback, Cb>
    explicit stop_callback(
        stop_token &&token, Cb &&cb
    ) noexcept(std::is_nothrow_constructible_v<Callback, Cb>)
        : m_cb(std::forward<Cb>(cb)) {
        if (auto &state = token.m_state) {
            if (state->register_callback(&m_cb)) {
                m_state.swap(state);
            }
        }
    }

    ~stop_callback() noexcept {
        if (m_state) {
            m_state->remove_callback(&m_cb);
        }
    }

    stop_callback(const stop_callback &) = delete;
    stop_callback &operator=(const stop_callback &) = delete;
    stop_callback(stop_callback &&) = delete;
    stop_callback &operator=(stop_callback &&) = delete;

  private:
    struct cb_impl : stop_token::stop_cb {
        template<typename Cb>
        explicit cb_impl(Cb &&cb)
            : stop_cb(&s_execute)
            , m_cb(std::forward<Cb>(cb)) {}

        Callback m_cb;

        [[__gnu__::__nonnull__]]
        static void s_execute(stop_cb *that) noexcept {
            Callback &cb = static_cast<cb_impl *>(that)->m_cb;
            std::forward<Callback>(cb)();
        }
    };

    cb_impl m_cb;
    stop_token::stop_state_ref m_state;
};

template<typename Callback>
stop_callback(stop_token, Callback) -> stop_callback<Callback>;

} // namespace co_context

#endif // __has_include(<stop_token>)
