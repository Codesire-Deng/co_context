#pragma once

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <sys/epoll.h>

namespace co_context::detail {

class epoll final {
  private:
    int epoll_fd = -1;
    int entries = 0;
    std::unique_ptr<::epoll_event[]> events_buf;

  public:
    explicit epoll() noexcept = default;

    ~epoll() noexcept {
        if (epoll_fd != -1) {
            ::close(epoll_fd);
        }
    };

    void init(int entries) noexcept {
        const int fd = ::epoll_create1(EPOLL_CLOEXEC);
        if (fd < 0) {
            perror("epoll_create1");
            std::terminate();
        }
        assert(entries > 0);
        events_buf = std::make_unique<::epoll_event[]>(entries);
        this->entries = entries;
        epoll_fd = fd;
    }

    [[nodiscard]]
    int fd() const noexcept {
        return epoll_fd;
    }

    int add(int fd, ::epoll_event event) /* NOLINT */ noexcept {
        return ::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    }

    int mod(int fd, ::epoll_event event) /* NOLINT */ noexcept {
        return ::epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    }

    int del(int fd, ::epoll_event event) /* NOLINT */ noexcept {
        return ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event);
    }

    std::span<const ::epoll_event> wait(int timeout = -1) noexcept {
        int res;
        do {
            res = ::epoll_wait(epoll_fd, events_buf.get(), entries, timeout);
        } while (res == -1 && errno == EINTR);
        assert(res >= 0);
        return {events_buf.get(), (size_t)res};
    }

    template<typename F>
        requires std::regular_invocable<F, ::epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, ::epoll_event *>>
    unsigned wait_and_for_each_event(F f, int timeout = -1) noexcept(
        noexcept(f(std::declval<::epoll_event *>()))
    ) {
        auto events = wait(timeout);
        for (const auto &e : events) {
            f(&e);
        }
        return events.size();
    }
};

} // namespace co_context::detail
