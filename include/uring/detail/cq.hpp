#pragma once

#include <uring/cq_entry.hpp>

namespace liburingcxx {

template<uint64_t uring_flags>
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
            // NOLINTBEGIN
            khead = (unsigned *)((uintptr_t)ring_ptr + off.head);
            ktail = (unsigned *)((uintptr_t)ring_ptr + off.tail);
            ring_mask = *(unsigned *)((uintptr_t)ring_ptr + off.ring_mask);
            ring_entries =
                *(unsigned *)((uintptr_t)ring_ptr + off.ring_entries);
            if (off.flags) {
                kflags = (unsigned *)((uintptr_t)ring_ptr + off.flags);
            }
            koverflow = (unsigned *)((uintptr_t)ring_ptr + off.overflow);
            cqes = (cq_entry *)((uintptr_t)ring_ptr + off.cqes);
            // NOLINTEND
        }

      public:
        template<uint64_t uring_flags>
        friend class ::liburingcxx::uring;
        completion_queue() noexcept = default;
        ~completion_queue() noexcept = default;

        template<uint64_t uring_flags>
        [[nodiscard]]
        cq_entry &cqe_at(unsigned offset) noexcept {
            constexpr int shift =
                bool(uring_flags & IORING_SETUP_CQE32) ? 1 : 0;
            return cqes[(offset & ring_mask) << shift];
        }

        template<uint64_t uring_flags>
        [[nodiscard]]
        const cq_entry &const_cqe_at(unsigned offset) const noexcept {
            constexpr int shift =
                bool(uring_flags & IORING_SETUP_CQE32) ? 1 : 0;
            return cqes[(offset & ring_mask) << shift];
        }
    };

    struct cq_entry_getter {
        unsigned submit;
        unsigned wait_num;
        unsigned get_flags;
        int size;
        void *arg;
    };

} // namespace detail

} // namespace liburingcxx
