#pragma once

#include <co_context/config.hpp>

#include <cassert>
#include <cerrno>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <memory>
#include <sys/epoll.h>
#include <utility>
#include <vector>

namespace co_context::detail {

struct epoll_fd_meta final {
    union user_data {
        int64_t i64;
        uint64_t u64;
        int32_t i32[2];
        uint32_t u32[2];
        void *ptr;
        std::coroutine_handle<> handle;

        user_data() noexcept {}

        user_data(const user_data &o) noexcept : u64(o.u64) {}

        user_data(user_data &&o) noexcept : u64(o.u64) {}

        user_data &operator=(const user_data &o) noexcept { // NOLINT
            this->u64 = o.u64;
            return *this;
        }

        user_data &operator=(user_data &&o) noexcept {
            this->u64 = o.u64;
            return *this;
        }

        ~user_data() noexcept {};
    };

    static_assert(sizeof(user_data) == 8);

    user_data in;
    user_data out;
    uint32_t interests = 0;
};

class epoll final {
  public:
    using epoll_event = ::epoll_event;
    using epoll_data_t = ::epoll_data_t;

  public:
    explicit epoll() noexcept = default;

    ~epoll() noexcept {
        if (epoll_fd_ != -1) {
            ::close(epoll_fd_);
        }
    };

    epoll(const epoll &) = delete;
    epoll &operator=(const epoll &) = delete;

    epoll(epoll &&o) noexcept
        : epoll_fd_(o.epoll_fd_)
        , max_entries_(o.max_entries_)
        , events_buf_(std::move(o.events_buf_))
        , ready_entries_(o.ready_entries_) {
        o.epoll_fd_ = -1;
        o.max_entries_ = 0;
        o.ready_entries_ = 0;
    }

    epoll &operator=(epoll &&o) noexcept {
        if (this == &o) {
            return *this;
        }
        this->epoll_fd_ = o.epoll_fd_;
        this->max_entries_ = o.max_entries_;
        this->events_buf_ = std::move(o.events_buf_);
        this->ready_entries_ = o.ready_entries_;
        o.epoll_fd_ = -1;
        o.max_entries_ = 0;
        o.ready_entries_ = 0;
        return *this;
    }

    void init(int entries) noexcept {
        assert(epoll_fd_ == -1);
        assert(events_buf_ == nullptr);
        assert(entries > 0);
        const int fd = ::epoll_create1(EPOLL_CLOEXEC);
        if (fd < 0) {
            perror("epoll_create1");
            std::terminate();
        }
        events_buf_ = std::make_unique<epoll_event[]>(entries);
        this->max_entries_ = entries;
        epoll_fd_ = fd;
    }

    [[nodiscard]]
    int fd() const noexcept {
        return epoll_fd_;
    }

    int add(int fd, const epoll_event &event) noexcept {
        return ::epoll_ctl(
            epoll_fd_, EPOLL_CTL_ADD, fd,
            /*NOLINT(*const-cast)*/ const_cast<epoll_event *>(&event)
        );
    }

    int mod(int fd, const epoll_event &event) noexcept {
        return ::epoll_ctl(
            epoll_fd_, EPOLL_CTL_MOD, fd,
            /*NOLINT(*const-cast)*/ const_cast<epoll_event *>(&event)
        );
    }

    int del(int fd) /*NOLINT(*function-const)*/ noexcept {
        return ::epoll_ctl(
            epoll_fd_, EPOLL_CTL_DEL, fd,
            reinterpret_cast<epoll_event *>(0x1000000UL)
        );
    }

    int mod_or_add(int fd, const epoll_event &event) noexcept {
        int res = mod(fd, event);
        return (res != -1 || errno != ENOENT) ? res : add(fd, event);
    }

    uint32_t wait(int timeout = -1) noexcept {
        int res;
        do {
            res = ::epoll_wait(
                epoll_fd_, events_buf_.get(), max_entries_, timeout
            );
        } while (res == -1 && errno == EINTR);
        assert(res >= 0);
        ready_entries_ = uint32_t(res);
        return ready_entries_;
    }

    template<typename F>
        requires std::regular_invocable<F, epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, epoll_event *>>
    uint32_t for_each_event(F &&f
    ) noexcept(noexcept(std::forward<F>(f)(std::declval<epoll_event *>()))) {
        uint32_t nr = std::exchange(ready_entries_, 0);
        for (auto *e = events_buf_.get(), *const end = e + nr; e != end; ++e) {
            std::forward<F>(f)(e);
        }
        return nr;
    }

    template<typename F>
        requires std::regular_invocable<F, epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, epoll_event *>>
    uint32_t wait_and_for_each_event(F &&f, int timeout = -1) noexcept(
        noexcept(std::forward<F>(f)(std::declval<epoll_event *>()))
    ) {
        wait(timeout);
        return for_each_event(std::forward<F>(f));
    }

    friend void swap(epoll &a, epoll &b) noexcept {
        std::swap(a.epoll_fd_, b.epoll_fd_);
        std::swap(a.max_entries_, b.max_entries_);
        std::swap(a.events_buf_, b.events_buf_);
        std::swap(a.ready_entries_, b.ready_entries_);
    }

  private:
    int epoll_fd_ = -1;
    int max_entries_ = 0;
    std::unique_ptr<epoll_event[]> events_buf_;
    uint32_t ready_entries_ = 0;
};

} // namespace co_context::detail
