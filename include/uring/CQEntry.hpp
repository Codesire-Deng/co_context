#pragma once

#include "uring/io_uring.h"
#include <cstring>

namespace liburingcxx {

template<unsigned URingFlags>
class URing;

class CQEntry final : private io_uring_cqe {
  public:
    template<unsigned URingFlags>
    friend class ::liburingcxx::URing;

    inline uint64_t getData() const noexcept { return this->user_data; }

    inline int32_t getRes() const noexcept { return this->res; }

    inline uint32_t getFlags() const noexcept { return this->flags; }
};

} // namespace liburingcxx