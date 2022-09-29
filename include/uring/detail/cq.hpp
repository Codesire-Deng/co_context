#pragma once

#include "uring/cq_entry.hpp"

namespace liburingcxx {

template<unsigned uring_flags>
class uring;

namespace detail {

    using cq_entry = ::liburingcxx::cq_entry;

    static_assert(sizeof(cq_entry) == sizeof(io_uring_cqe));
    static_assert(sizeof(cq_entry) == 16);
    static_assert(alignof(cq_entry) == 8);

    class completion_queue final {
      private:
        unsigned *khead;
        unsigned *ktail;
        unsigned ring_mask;
        unsigned ring_entries;
        unsigned *kflags;
        unsigned *koverflow;
        cq_entry *cqes;

        size_t ring_sz;
        void *ring_ptr;

      private:
        void set_offset(const io_cqring_offsets &off) noexcept {
            khead = (unsigned *)((uintptr_t)ring_ptr + off.head);
            ktail = (unsigned *)((uintptr_t)ring_ptr + off.tail);
            ring_mask = *(unsigned *)((uintptr_t)ring_ptr + off.ring_mask);
            ring_entries =
                *(unsigned *)((uintptr_t)ring_ptr + off.ring_entries);
            if (off.flags)
                kflags = (unsigned *)((uintptr_t)ring_ptr + off.flags);
            koverflow = (unsigned *)((uintptr_t)ring_ptr + off.overflow);
            cqes = (cq_entry *)((uintptr_t)ring_ptr + off.cqes);
        }

      public:
        template<unsigned uring_flags>
        friend class ::liburingcxx::uring;
        completion_queue() noexcept = default;
        ~completion_queue() noexcept = default;
    };

    struct cq_entry_getter {
        unsigned submit;
        unsigned wait_num;
        unsigned getFlags;
        int size;
        void *arg;
    };

} // namespace detail

} // namespace liburingcxx