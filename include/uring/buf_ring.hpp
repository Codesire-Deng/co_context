#pragma once

#include "uring/io_uring.h"

namespace liburingcxx {

template<unsigned uring_flags>
class uring;

class buf_ring final : private io_uring_buf_ring {
  public:
    inline void init() noexcept { this->tail = 0; }

    inline void
    add(void *addr,
        unsigned int len,
        unsigned short bid,
        int mask,
        int buf_offset) noexcept {
        const int index = (this->tail + buf_offset) & mask;

        io_uring_buf *const buf = this->bufs + index;
        buf->addr =
            static_cast<unsigned long>(reinterpret_cast<uintptr_t>(addr));
        buf->len = len;
        buf->bid = bid;
    }

    inline void advance(int count) noexcept {
        const unsigned short new_tail = this->tail + count;
        io_uring_smp_store_release(&this->tail, new_tail);
    }

  public:
    static inline constexpr int mask_of(uint32_t ring_entries) noexcept {
        return ring_entries - 1;
    }

  public:
    template<unsigned uring_flags>
    friend class ::liburingcxx::uring;
};

} // namespace liburingcxx