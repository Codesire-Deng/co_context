#pragma once

#include "uring/CQEntry.hpp"

namespace liburingcxx {

template<unsigned URingFlags>
class URing;

namespace detail {

    using CQEntry = ::liburingcxx::CQEntry;

    static_assert(sizeof(CQEntry) == sizeof(io_uring_cqe));

    class CompletionQueue final {
      private:
        unsigned *khead;
        unsigned *ktail;
        unsigned ring_mask;
        unsigned ring_entries;
        unsigned *kflags;
        unsigned *koverflow;
        CQEntry *cqes;

        size_t ring_sz;
        void *ring_ptr;

      private:
        void setOffset(const io_cqring_offsets &off) noexcept {
            khead = (unsigned *)((uintptr_t)ring_ptr + off.head);
            ktail = (unsigned *)((uintptr_t)ring_ptr + off.tail);
            ring_mask = *(unsigned *)((uintptr_t)ring_ptr + off.ring_mask);
            ring_entries =
                *(unsigned *)((uintptr_t)ring_ptr + off.ring_entries);
            if (off.flags)
                kflags = (unsigned *)((uintptr_t)ring_ptr + off.flags);
            koverflow = (unsigned *)((uintptr_t)ring_ptr + off.overflow);
            cqes = (CQEntry *)((uintptr_t)ring_ptr + off.cqes);
        }

      public:
        template<unsigned URingFlags>
        friend class ::liburingcxx::URing;
        CompletionQueue() noexcept = default;
        ~CompletionQueue() noexcept = default;
    };

    struct CQEGetter {
        unsigned submit;
        unsigned waitNum;
        unsigned getFlags;
        int size;
        void *arg;
    };

} // namespace detail

} // namespace liburingcxx