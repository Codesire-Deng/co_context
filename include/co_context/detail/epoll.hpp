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
  public:
    using epoll_event = ::epoll_event;
    using epoll_data_t = ::epoll_data_t;

  private:
    int epoll_fd = -1;
    int entries = 0;
    std::unique_ptr<epoll_event[]> events_buf;
    uint32_t ready_entries;

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
        events_buf = std::make_unique<epoll_event[]>(entries);
        this->entries = entries;
        epoll_fd = fd;
    }

    [[nodiscard]]
    int fd() const noexcept {
        return epoll_fd;
    }

    int add(int fd, const epoll_event &event) /* NOLINT */ noexcept {
        return ::epoll_ctl(
            epoll_fd, EPOLL_CTL_ADD, fd, const_cast<epoll_event *>(&event)
        );
    }

    int mod(int fd, const epoll_event &event) /* NOLINT */ noexcept {
        return ::epoll_ctl(
            epoll_fd, EPOLL_CTL_MOD, fd, const_cast<epoll_event *>(&event)
        );
    }

    int del(int fd, const epoll_event &event) /* NOLINT */ noexcept {
        return ::epoll_ctl(
            epoll_fd, EPOLL_CTL_DEL, fd, const_cast<epoll_event *>(&event)
        );
    }

    int mod_or_add(int fd, const epoll_event &event) /* NOLINT */ noexcept {
        int res = mod(fd, event);
        return res != ENOENT ? res : add(fd, event);
    }

    uint32_t wait(int timeout = -1) noexcept {
        int res;
        do {
            res = ::epoll_wait(epoll_fd, events_buf.get(), entries, timeout);
        } while (res == -1 && errno == EINTR);
        assert(res >= 0);
        ready_entries = uint32_t(res);
        return ready_entries;
    }

    template<typename F>
        requires std::regular_invocable<F, epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, epoll_event *>>
    uint32_t for_each_event(F &&f
    ) noexcept(noexcept(f(std::declval<epoll_event *>()))) {
        for (auto *e = events_buf.get(), *const end = e + ready_entries;
             e != end; ++e) {
            f(e);
        }
        return ready_entries;
    }

    template<typename F>
        requires std::regular_invocable<F, epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, epoll_event *>>
    uint32_t wait_and_for_each_event(F &&f, int timeout = -1) noexcept(
        noexcept(f(std::declval<epoll_event *>()))
    ) {
        wait(timeout);
        for_each_event(std::forward<F>(f));
        return ready_entries;
    }
};

} // namespace co_context::detail
