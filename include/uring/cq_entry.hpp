#pragma once

#include <uring/io_uring.h>

#include <cstdint>
#include <cstring>

namespace liburingcxx {

using cq_entry = io_uring_cqe;

} // namespace liburingcxx
