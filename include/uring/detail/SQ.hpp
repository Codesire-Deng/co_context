#pragma once

#include "uring/SQEntry.hpp"

namespace liburingcxx {

template<unsigned URingFlags>
class URing;

namespace detail {

    using SQEntry = ::liburingcxx::SQEntry;

    static_assert(sizeof(SQEntry) == sizeof(io_uring_sqe));

    class SubmissionQueue final {
      private:
        unsigned *khead;
        unsigned *ktail;
        unsigned ring_mask;
        unsigned ring_entries;
        unsigned *kflags;
        unsigned *kdropped;
        unsigned *array;
        SQEntry *sqes;

        unsigned sqe_head; // memset to 0 during URing()
        unsigned sqe_tail; // memset to 0 during URing()

        size_t ring_sz;
        void *ring_ptr;

        unsigned pad[4];

      private:
        void setOffset(const io_sqring_offsets &off) noexcept {
            khead = (unsigned *)((uintptr_t)ring_ptr + off.head);
            ktail = (unsigned *)((uintptr_t)ring_ptr + off.tail);
            ring_mask = *(unsigned *)((uintptr_t)ring_ptr + off.ring_mask);
            ring_entries =
                *(unsigned *)((uintptr_t)ring_ptr + off.ring_entries);
            kflags = (unsigned *)((uintptr_t)ring_ptr + off.flags);
            kdropped = (unsigned *)((uintptr_t)ring_ptr + off.dropped);
            array = (unsigned *)((uintptr_t)ring_ptr + off.array);
        }

        /**
         * @brief Sync internal state with kernel ring state on the SQ side.
         *
         * @return unsigned number of pending items in the SQ ring, for the
         * shared ring.
         */
        unsigned flush() noexcept {
            const unsigned mask = ring_mask;
            unsigned tail = *ktail;
            unsigned to_submit = sqe_tail - sqe_head;
            if (to_submit == 0) return tail - *khead; // see below

            /*
             * Fill in sqes that we have queued up, adding them to the kernel
             * ring
             */
            do {
                array[tail & mask] = sqe_head & mask;
                tail++;
                sqe_head++;
            } while (--to_submit);

            /*
             * Ensure that the kernel sees the SQE updates before it sees the
             * tail update.
             */
            io_uring_smp_store_release(ktail, tail);

            /*
             * This _may_ look problematic, as we're not supposed to be reading
             * SQ->head without acquire semantics. When we're in SQPOLL mode,
             * the kernel submitter could be updating this right now. For
             * non-SQPOLL, task itself does it, and there's no potential race.
             * But even for SQPOLL, the load is going to be potentially
             * out-of-date the very instant it's done, regardless or whether or
             * not it's done atomically. Worst case, we're going to be
             * over-estimating what we can submit. The point is, we need to be
             * able to deal with this situation regardless of any perceived
             * atomicity.
             */
            return tail - *khead;
        }

        inline SQEntry *getSQEntry() noexcept {
            const unsigned int head = io_uring_smp_load_acquire(khead);
            const unsigned int next = sqe_tail + 1;
            SQEntry *sqe = nullptr;
            if (next - head <= ring_entries) {
                sqe = sqes + (sqe_tail & ring_mask);
                sqe_tail = next;
            }
            return sqe;
        }

      public:
        template<unsigned URingFlags>
        friend class ::liburingcxx::URing;
        SubmissionQueue() noexcept = default;
        ~SubmissionQueue() noexcept = default;
    };

} // namespace detail

} // namespace liburingcxx