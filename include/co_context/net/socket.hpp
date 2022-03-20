#pragma once

#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
// #include "co_context/defer.hpp"
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

    auto recv(std::span<char> buf, int flags) noexcept {
        return lazy::recv(sockfd, buf, flags);
    }

    auto send(std::span<const char> buf, int flags) noexcept {
        return lazy::send(sockfd, buf, flags);
    }

    auto close() noexcept {
        int tmp = sockfd;
        sockfd = -1;
        return lazy::close(tmp);
    }

    auto shutdown_write() noexcept { return lazy::shutdown(sockfd, SHUT_WR); }

    // factory methods
    static socket create_tcp(sa_family_t family); // AF_INET or AF_INET6
    static socket create_udp(sa_family_t family); // AF_INET or AF_INET6

  private:
    int sockfd;
};

inline socket &socket::bind(const inet_address &addr) {
    int res = ::bind(sockfd, addr.get_sockaddr(), addr.length());
    if (res != 0) {
        perror("socket::bind");
        abort();
    }

    return *this;
}

inline socket &socket::listen() {
    int res = ::listen(sockfd, SOMAXCONN);
    if (res != 0) {
        perror("socket::listen");
        abort();
    }

    return *this;
}

inline socket &socket::set_reuse_addr(bool on) {
    int optval = on;
    if (::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval)
        < 0) {
        perror("socket::setReuseAddr");
    }
    return *this;
}

inline socket &socket::set_tcp_no_delay(bool on) {
    int optval = on ? 1 : 0;
    if (::setsockopt(
            sockfd, IPPROTO_TCP, TCP_NODELAY, &optval,
            static_cast<socklen_t>(sizeof optval))
        < 0) {
        perror("socket::setTcpNoDelay");
    }
    return *this;
}

inline inet_address socket::get_local_addr() const {
    struct sockaddr_storage localaddr;
    socklen_t addrlen = sizeof localaddr;
    struct sockaddr *addr = reinterpret_cast<struct sockaddr *>(&localaddr);
    if (::getsockname(sockfd, addr, &addrlen) < 0) {
        perror("socket::getLocalAddr");
    }
    return inet_address(*addr);
}

inline inet_address socket::get_peer_addr() const {
    struct sockaddr_storage peeraddr;
    socklen_t addrlen = sizeof peeraddr;
    struct sockaddr *addr = reinterpret_cast<struct sockaddr *>(&peeraddr);
    if (::getpeername(sockfd, addr, &addrlen) < 0) {
        perror("socket::getPeerAddr");
    }
    return inet_address(*addr);
}

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