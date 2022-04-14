#pragma once

#include "co_context/config.hpp"
#include <cstdio>

namespace co_context {

namespace detail {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"

    template<typename... T>
    void log(const char *__restrict__ fmt, const T &...args) {
        printf(fmt, args...);
    }

    template<typename... T>
    void err(const char *__restrict__ fmt, const T &...args) {
        fprintf(stderr, fmt, args...);
    }

#pragma GCC diagnostic pop

} // namespace detail

namespace log {
    template<typename... T>
    void v(const char *__restrict__ fmt, const T &...args) {
        if constexpr (config::log_level <= config::level::v)
            detail::log(fmt, args...);
    }

    template<typename... T>
    void i(const char *__restrict__ fmt, const T &...args) {
        if constexpr (config::log_level <= config::level::i)
            detail::log(fmt, args...);
    }

    template<typename... T>
    void d(const char *__restrict__ fmt, const T &...args) {
        if constexpr (config::log_level <= config::level::d)
            detail::log(fmt, args...);
    }

    template<typename... T>
    void w(const char *__restrict__ fmt, const T &...args) {
        if constexpr (config::log_level <= config::level::w)
            detail::err(fmt, args...);
    }

    template<typename... T>
    void e(const char *__restrict__ fmt, const T &...args) {
        if constexpr (config::log_level <= config::level::e)
            detail::err(fmt, args...);
    }
} // namespace log

} // namespace co_context
