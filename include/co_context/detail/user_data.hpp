#pragma once
#include <cstdint>

namespace co_context::detail {

enum class reserved_user_data : uint64_t { co_spawn_event, nop };

} // namespace co_context::detail
