#pragma once
#include <cstdint>

namespace co_context::detail {

enum class reserved_user_data : uint64_t {
#if CO_CONTEXT_IS_USING_EVENTFD
    co_spawn_event,
#endif
    nop,
    none
};

enum class user_data_type : uint8_t {
    task_info_ptr,
    coroutine_handle,
    task_info_ptr__link_sqe,
    msg_ring,
    none
};

static_assert(uint8_t(user_data_type::none) <= 8);

} // namespace co_context::detail
