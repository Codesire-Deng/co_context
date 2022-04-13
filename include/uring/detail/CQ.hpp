#pragma once

#include "uring/io_uring.h"

namespace liburingcxx {

template<unsigned URingFlags>
class URing;

namespace detail {

    class CompletionQueue final {
      private:
        unsigned *khead;
        unsigned *ktail;
        unsigned *kring_mask;
        unsigned *kring_entries;
        unsigned *kflags;
        unsigned *koverflow;
        struct io_uring_cqe *cqes;

        size_t ring_sz;
        void *ring_ptr;

        unsigned pad[4];

      private:
        void setOffset(const io_cqring_offsets &off) noexcept {
            khead = (unsigned *)((char *)ring_ptr + off.head);
            ktail = (unsigned *)((char *)ring_ptr + off.tail);
            kring_mask = (unsigned *)((char *)ring_ptr + off.ring_mask);
            kring_entries = (unsigned *)((char *)ring_ptr + off.ring_entries);
            if (off.flags) kflags = (unsigned *)((char *)ring_ptr + off.flags);
            koverflow = (unsigned *)((char *)ring_ptr + off.overflow);
            cqes = (io_uring_cqe *)((char *)ring_ptr + off.cqes);
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