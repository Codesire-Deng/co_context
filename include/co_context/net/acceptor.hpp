#pragma once
#include "./socket.hpp"

#include <memory>

namespace co_context {

class inet_address;

class acceptor {
  public:
    explicit acceptor(const inet_address &listen_addr);

    ~acceptor() = default;
    acceptor(acceptor &&) = default;
    acceptor &operator=(acceptor &&) = default;

    [[nodiscard]]
    auto accept(int flags = 0) noexcept {
        return lazy::accept(listen_socket.fd(), nullptr, nullptr, flags);
    }

    [[nodiscard]]
    int listen_fd() const noexcept {
        return listen_socket.fd();
    }

    // socket acceptSocketOrDie();

  private:
    socket listen_socket;
};

inline acceptor::acceptor(const inet_address &listen_addr)
    : listen_socket(socket::create_tcp(listen_addr.family())) {
    listen_socket.set_reuse_addr(true).bind(listen_addr).listen();
}

} // namespace co_context
