#pragma once

#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
// #include "co_context/utility/defer.hpp"
// #include "co_context/config.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/eager_io.hpp"
#include "co_context/net/inet_address.hpp"

namespace co_context {

class inet_address;

class socket {
  public:
    explicit socket(int sockfd) noexcept : sockfd(sockfd) {
        assert(sockfd >= 0);
    };

    ~socket() noexcept {
        // TODO check ~socket()
        /*
        if (sockfd >= 0) {
            [[maybe_unused]] int res = ::close(sockfd);
            assert(res == 0);
        }
        */
    };

    socket(socket &&other) noexcept {
        sockfd = other.sockfd;
        other.sockfd = -1;
    }

    socket &operator=(socket &&other) noexcept {
        assert(this != std::addressof(other));
        sockfd = other.sockfd;
        other.sockfd = -1;
        return *this;
    }

    void swap(socket &other) noexcept {
        int tmp = sockfd;
        sockfd = other.sockfd;
        other.sockfd = tmp;
    }

    int fd() const noexcept { return sockfd; }

    socket &bind(const inet_address &addr);

    socket &listen();

    socket &set_reuse_addr(bool on);

    socket &set_tcp_no_delay(bool on);

    inet_address get_local_addr() const;

    inet_address get_peer_addr() const;

    auto connect(const inet_address &addr) noexcept {
        return lazy::connect(sockfd, addr.get_sockaddr(), addr.length());
    }

    auto eager_connect(const inet_address &addr) noexcept {
        return eager::connect(sockfd, addr.get_sockaddr(), addr.length());
    }

    auto recv(std::span<char> buf, int flags = 0) noexcept {
        return lazy::recv(sockfd, buf, flags);
    }

    auto eager_recv(std::span<char> buf, int flags = 0) noexcept {
        return eager::recv(sockfd, buf, flags);
    }

    auto send(std::span<const char> buf, int flags = 0) noexcept {
        return lazy::send(sockfd, buf, flags);
    }

    auto eager_send(std::span<const char> buf, int flags = 0) noexcept {
        return eager::send(sockfd, buf, flags);
    }

    auto close() noexcept {
        int tmp = sockfd;
        sockfd = -1;
        return lazy::close(tmp);
    }

    auto eager_close() noexcept {
        int tmp = sockfd;
        sockfd = -1;
        return eager::close(tmp);
    }

    auto shutdown_write() noexcept { return lazy::shutdown(sockfd, SHUT_WR); }

    auto eager_shutdown_write() noexcept {
        return eager::shutdown(sockfd, SHUT_WR);
    }

    // factory methods
    static socket create_tcp(sa_family_t family); // AF_INET or AF_INET6
    static socket create_udp(sa_family_t family); // AF_INET or AF_INET6

  private:
    int sockfd;
};

inline socket socket::create_tcp(sa_family_t family) {
    int sockfd = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    assert(sockfd >= 0);
    return socket(sockfd);
}

inline socket socket::create_udp(sa_family_t family) {
    int sockfd = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    assert(sockfd >= 0);
    return socket(sockfd);
}

} // namespace co_context