#pragma once

#include "co_context/lazy_io.hpp"
#include "co_context/net/inet_address.hpp"
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace co_context {

class inet_address;

class socket {
  public:
    explicit socket(int sockfd) noexcept : sockfd(sockfd) {
        assert(sockfd >= 0);
    };

    // TODO check if ~socket() need `close(sockfd)`
    ~socket() noexcept = default;

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
        const int tmp = sockfd;
        sockfd = other.sockfd;
        other.sockfd = tmp;
    }

    [[nodiscard]] int fd() const noexcept { return sockfd; }

    socket &bind(const inet_address &addr);

    socket &listen();

    socket &set_reuse_addr(bool on);

    socket &set_tcp_no_delay(bool on);

    [[nodiscard]] inet_address get_local_addr() const;

    [[nodiscard]] inet_address get_peer_addr() const;

    [[nodiscard]] auto connect(const inet_address &addr) const noexcept {
        return lazy::connect(sockfd, addr.get_sockaddr(), addr.length());
    }

    [[nodiscard]] auto recv(std::span<char> buf, int flags = 0) const noexcept {
        return lazy::recv(sockfd, buf, flags);
    }

    [[nodiscard]] auto
    send(std::span<const char> buf, int flags = 0) const noexcept {
        return lazy::send(sockfd, buf, flags);
    }

    [[nodiscard]] auto close() noexcept {
        const int tmp = sockfd;
        sockfd = -1;
        return lazy::close(tmp);
    }

    [[nodiscard]] auto shutdown_write() const noexcept {
        return lazy::shutdown(sockfd, SHUT_WR);
    }

    // factory methods
    static socket create_tcp(sa_family_t family); // AF_INET or AF_INET6
    static socket create_udp(sa_family_t family); // AF_INET or AF_INET6

  private:
    int sockfd;
};

inline socket socket::create_tcp(sa_family_t family) {
    const int sockfd =
        ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    assert(sockfd >= 0);
    return socket{sockfd};
}

inline socket socket::create_udp(sa_family_t family) {
    const int sockfd = ::socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    assert(sockfd >= 0);
    return socket{sockfd};
}

} // namespace co_context
