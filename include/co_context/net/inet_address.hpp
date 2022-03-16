#pragma once

#include <netinet/in.h>
#include <sys/socket.h>
#include <string>
#include <string_view>
#include <arpa/inet.h>
#include <cstring>
#include <stddef.h>
#include <netdb.h>
#include <cassert>
#include "co_context/defer.hpp"
#include "co_context/config.hpp"

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
    inet_address(uint16_t port, bool isIpv6 = false) noexcept;

    /**
     * @brief for Sockets API
     */
    explicit inet_address(const struct sockaddr &saddr) noexcept;

    sa_family_t family() const noexcept { return addr.sin_family; }

    uint16_t port() const noexcept { return ntohs(addr.sin_port); }

    inet_address &reset_port(uint16_t port) noexcept {
        addr.sin_port = htons(port);
        return *this;
    }

    std::string to_ip() const;
    std::string to_ip_port() const;

    const struct sockaddr *get_sockaddr() const noexcept {
        return reinterpret_cast<const struct sockaddr *>(&addr6);
    }

    socklen_t length() const noexcept {
        return family() == AF_INET6 ? sizeof(addr6) : sizeof(addr);
    }

    bool operator==(const inet_address &rhs) const noexcept;

    static bool
    resolve(std::string_view hostname, uint16_t port, inet_address &);

    static std::vector<inet_address>
    resolve_all(std::string_view hostname, uint16_t port, inet_address &);

  private:
    union {
        struct sockaddr_in addr;
        struct sockaddr_in6 addr6;
    };
};

/**
 * @brief for connecting
 */
inet_address::inet_address(std::string_view ip, uint16_t port) noexcept {
    reset_port(port);
    int res = 0;
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
inet_address::inet_address(uint16_t port, bool isIpv6) noexcept {
    static_assert(offsetof(inet_address, addr6) == 0, "addr6 offset 0");
    static_assert(offsetof(inet_address, addr) == 0, "addr offset 0");

    if (isIpv6) {
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        if constexpr (config::loopback_only)
            addr6.sin6_addr = in6addr_loopback;
        else
            addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port);
    } else {
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        if constexpr (config::loopback_only)
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        else
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

    if (family() == AF_INET6)
        return "[" + to_ip() + "]" + buf;
    else
        return to_ip() + buf;
}

bool inet_address::operator==(const inet_address &rhs) const noexcept {
    if (family() != rhs.family()) return false;
    if (family() == AF_INET) {
        return addr.sin_port == rhs.addr.sin_port
               && addr.sin_addr.s_addr == rhs.addr.sin_addr.s_addr;
    } else {
        return addr6.sin6_port == rhs.addr6.sin6_port
               && memcmp(
                      &addr6.sin6_addr, &rhs.addr6.sin6_addr,
                      sizeof(addr6.sin6_addr))
                      == 0;
    }
}

bool inet_address::resolve(
    std::string_view hostname, uint16_t port, inet_address &out) {

    auto addrs = resolve_all(hostname, port, out);
    
    if (addrs.empty()) return false;
    out = addrs.front();
    return true;
}

std::vector<inet_address> inet_address::resolve_all(
    std::string_view hostname, uint16_t port, inet_address &) {

    std::vector<inet_address> ret;
    struct addrinfo *result = nullptr;
    int err = getaddrinfo(hostname.data(), nullptr, nullptr, &result);
    if (err != 0) {
        if (err == EAI_SYSTEM)
            perror("inet_address::resolve");
        else
            fprintf(stderr, "inet_address::resolve: %s\n", gai_strerror(err));
        return ret;
    }

    assert(result != nullptr);
    Defer guard{[result] {
        freeaddrinfo(result);
    }};

    for (struct addrinfo* i=result; i!=nullptr; i=i->ai_next) {
        ret.emplace_back(*i->ai_addr);
        ret.back().reset_port(port);
    }

    return ret;
}

} // namespace co_context