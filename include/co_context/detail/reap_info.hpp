#pragma once

#include <co_context/utility/bit.hpp>
#include <coroutine>
#include <cstdint>

namespace liburingcxx {

class sq_entry;

} // namespace liburingcxx

namespace co_context {

namespace detail {

    struct task_info;

    struct reap_info {
        union {
            std::coroutine_handle<> handle;
            task_info *io_info;
        };

        int32_t result;
        uint32_t flags;

        inline static constexpr uint32_t co_spawn_flag = bit_top<uint32_t>();

        reap_info() noexcept : io_info(nullptr), result(0), flags(0) {}

        reap_info(task_info *io_info, int32_t result, uint32_t flags) noexcept
            : io_info(io_info), result(result), flags(flags) {}

        explicit reap_info(std::coroutine_handle<> handle) noexcept
            : handle(handle), flags(co_spawn_flag) {}

        bool is_co_spawn() const noexcept { return flags == co_spawn_flag; }
    };

} // namespace detail

} // namespace co_context
