#pragma once

#include <co_context/utility/defer.hpp>

#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <vector>

namespace co_context {

class inet_address {
  public:
    /**
     * @brief Invalid address
     */
    inet_address() noexcept { addr.sin_family = AF_UNSPEC; }

    /**
     * @brief for connecting
     */
    inet_address(std::string_view ip, uint16_t port) noexcept;

    /**
     * @brief for listening
     */
    explicit inet_address(uint16_t port, bool is_ipv6 = false) noexcept;

    /**
     * @brief for Sockets API
     */
    explicit inet_address(const struct sockaddr &saddr) noexcept;

    [[nodiscard]]
    sa_family_t family() const noexcept {
        return addr.sin_family;
    }

    [[nodiscard]]
    uint16_t port() const noexcept {
        return ntohs(addr.sin_port);
    }

    inet_address &reset_port(uint16_t port) noexcept {
        addr.sin_port = htons(port);
        return *this;
    }

    [[nodiscard]]
    std::string to_ip() const;
    [[nodiscard]]
    std::string to_ip_port() const;

    [[nodiscard]]
    const struct sockaddr *get_sockaddr() const noexcept {
        return reinterpret_cast<const struct sockaddr *>(&addr6);
    }

    [[nodiscard]]
    socklen_t length() const noexcept {
        return family() == AF_INET6 ? sizeof(addr6) : sizeof(addr);
    }

    bool operator==(const inet_address &rhs) const noexcept;

    static bool
    resolve(std::string_view hostname, uint16_t port, inet_address &out);

    static std::vector<inet_address> resolve_all(
        std::string_view hostname, uint16_t port, const ::addrinfo *hints
    );

  private:
    union {
        struct sockaddr_in addr;
        struct sockaddr_in6 addr6;
    };
};

} // namespace co_context
