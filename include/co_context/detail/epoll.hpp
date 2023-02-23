#pragma once

#include <cassert>
#include <cerrno>
#include <co_context/config.hpp>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <sys/epoll.h>
#include <utility>
#include <vector>

namespace co_context::detail {

struct epoll_fd_data {
    union user_data {
        int64_t i64;
        uint64_t u64;
        void *ptr;
    };

    uint32_t interests;
    user_data in;
    user_data out;
};

class epoll final {
  public:
    using epoll_event = ::epoll_event;
    using epoll_data_t = ::epoll_data_t;

  public:
    explicit epoll() noexcept = default;

    ~epoll() noexcept {
        if (epoll_fd != -1) {
            ::close(epoll_fd);
        }
    };

    epoll(const epoll &) = delete;
    epoll &operator=(const epoll &) = delete;

    epoll(epoll &&o) noexcept
        : epoll_fd(o.epoll_fd)
        , entries(o.entries)
        , events_buf(std::move(o.events_buf))
        , ready_entries(o.ready_entries)
        , fd_data(std::move(o.fd_data)) {
        o.epoll_fd = -1;
        o.entries = 0;
        o.ready_entries = 0;
    }

    epoll &operator=(epoll &&o) noexcept {
        if (this == &o) {
            return *this;
        }
        this->epoll_fd = o.epoll_fd;
        this->entries = o.entries;
        this->events_buf = std::move(o.events_buf);
        this->ready_entries = o.ready_entries;
        this->fd_data = std::move(o.fd_data);
        o.epoll_fd = -1;
        o.entries = 0;
        o.ready_entries = 0;
        return *this;
    }

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
        uint32_t nr = std::exchange(ready_entries, 0);
        for (auto *e = events_buf.get(), *const end = e + nr; e != end; ++e) {
            f(e);
        }
        return nr;
    }

    template<typename F>
        requires std::regular_invocable<F, epoll_event *>
                 && std::is_void_v<std::invoke_result_t<F, epoll_event *>>
    uint32_t wait_and_for_each_event(F &&f, int timeout = -1) noexcept(
        noexcept(f(std::declval<epoll_event *>()))
    ) {
        wait(timeout);
        return for_each_event(std::forward<F>(f));
    }

    friend void swap(epoll &a, epoll &b) noexcept {
        std::swap(a.epoll_fd, b.epoll_fd);
        std::swap(a.entries, b.entries);
        std::swap(a.events_buf, b.events_buf);
        std::swap(a.ready_entries, b.ready_entries);
        std::swap(a.fd_data, b.fd_data);
    }

  private:
    int epoll_fd = -1;
    int entries = 0;
    std::unique_ptr<epoll_event[]> events_buf;
    uint32_t ready_entries = 0;

  public:
    std::vector<epoll_fd_data> fd_data{config::default_epoll_entries};

    auto &make_fd_data(int fd) noexcept {
        if (fd >= fd_data.size()) {
            fd_data.resize((size_t)fd * 2);
        }
        return fd_data[fd];
    }
};

} // namespace co_context::detail
