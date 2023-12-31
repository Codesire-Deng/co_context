#include <co_context/config/net.hpp>
#include <co_context/log/log.hpp>
#include <co_context/net/inet_address.hpp>

#include <arpa/inet.h>
#include <string_view>
#include <sys/socket.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>

namespace co_context {

/**
 * @brief for connecting
 */
inet_address::inet_address(std::string_view ip, uint16_t port) noexcept {
    reset_port(port);
    [[maybe_unused]] int res = 0;
    if (ip.find(':') == ip.npos) {
        res = ::inet_pton(AF_INET, ip.data(), &addr.sin_addr);
        addr.sin_family = AF_INET;
    } else {
        res = ::inet_pton(AF_INET6, ip.data(), &addr6.sin6_addr);
        addr6.sin6_family = AF_INET6;
    }

    assert(res == 1 && "Invalid IP format");
}

/**
 * @brief for listening
 */
inet_address::inet_address(uint16_t port, bool is_ipv6) noexcept {
    static_assert(offsetof(inet_address, addr6) == 0, "addr6 offset 0");
    static_assert(offsetof(inet_address, addr) == 0, "addr offset 0");

    if (is_ipv6) {
        std::memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        if constexpr (config::is_loopback_only) {
            addr6.sin6_addr = in6addr_loopback;
        } else {
            addr6.sin6_addr = in6addr_any;
        }
        addr6.sin6_port = htons(port);
    } else {
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        if constexpr (config::is_loopback_only) {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        } else {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        addr.sin_port = htons(port);
    }
}

/**
 * @brief for listening
 */
inet_address::inet_address(const struct sockaddr &saddr) noexcept {
    static_assert(offsetof(inet_address, addr6) == 0, "addr6 offset 0");
    static_assert(offsetof(inet_address, addr) == 0, "addr offset 0");

    if (saddr.sa_family == AF_INET) {
        memcpy(&addr, &saddr, sizeof(addr));
    } else if (saddr.sa_family == AF_INET6) {
        memcpy(&addr6, &saddr, sizeof(addr6));
    } else {
        assert(false && "Invalid sa_family");
    }
}

std::string inet_address::to_ip() const {
    char buf[INET6_ADDRSTRLEN + 1];
    if (family() == AF_INET) {
        ::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    } else {
        ::inet_ntop(AF_INET6, &addr6.sin6_addr, buf, sizeof(buf));
    }
    return buf;
}

std::string inet_address::to_ip_port() const {
    char buf[32];
    snprintf(buf, sizeof buf, ":%u", port());

    if (family() == AF_INET6) {
        return "[" + to_ip() + "]" + buf;
    }

    return to_ip() + buf;
}

bool inet_address::operator==(const inet_address &rhs) const noexcept {
    if (family() != rhs.family()) {
        return false;
    }
    if (family() == AF_INET) {
        return addr.sin_port == rhs.addr.sin_port
               && addr.sin_addr.s_addr == rhs.addr.sin_addr.s_addr;
    }

    return addr6.sin6_port == rhs.addr6.sin6_port
           && memcmp(
                  &addr6.sin6_addr, &rhs.addr6.sin6_addr,
                  sizeof(addr6.sin6_addr)
              ) == 0;
}

bool inet_address::resolve(
    std::string_view hostname, uint16_t port, inet_address &out
) {
    ::addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_flags |= AI_ADDRCONFIG; // for connect. see getaddrinfo(3)
    auto addrs = resolve_all(hostname, port, &hints);

    for (auto &addr : addrs) {
        log::v("ip port: %s\n", addr.to_ip_port().data());
    }

    if (addrs.empty()) {
        return false;
    }
    out = addrs.back(); // for ipv4 precedence
    return true;
}

std::vector<inet_address> inet_address::resolve_all(
    std::string_view hostname, uint16_t port, const ::addrinfo *hints
) {
    std::vector<inet_address> ret;
    struct addrinfo *result = nullptr;
    const int err = getaddrinfo(hostname.data(), nullptr, hints, &result);
    if (err != 0) {
        if (err == EAI_SYSTEM) {
            perror("inet_address::resolve");
        } else {
            fprintf(stderr, "inet_address::resolve: %s\n", gai_strerror(err));
        }
        return ret;
    }

    assert(result != nullptr);
    const defer guard{[result] {
        freeaddrinfo(result);
    }};

    for (struct addrinfo *i = result; i != nullptr; i = i->ai_next) {
        ret.emplace_back(*i->ai_addr);
        ret.back().reset_port(port);
    }

    return ret;
}

} // namespace co_context
