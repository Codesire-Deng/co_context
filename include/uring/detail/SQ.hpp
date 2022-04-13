#pragma once

#include "uring/io_uring.h"

namespace liburingcxx {

template<unsigned URingFlags>
class URing;

namespace detail {

    class SubmissionQueue final {
      private:
        unsigned *khead;
        unsigned *ktail;
        unsigned *kring_mask;
        unsigned *kring_entries;
        unsigned *kflags;
        unsigned *kdropped;
        unsigned *array;
        struct io_uring_sqe *sqes;

        unsigned sqe_head; // memset to 0 during URing()
        unsigned sqe_tail; // memset to 0 during URing()

        size_t ring_sz;
        void *ring_ptr;

        unsigned pad[4];

      private:
        void setOffset(const io_sqring_offsets &off) noexcept {
            khead = (unsigned *)((char *)ring_ptr + off.head);
            ktail = (unsigned *)((char *)ring_ptr + off.tail);
            kring_mask = (unsigned *)((char *)ring_ptr + off.ring_mask);
            kring_entries = (unsigned *)((char *)ring_ptr + off.ring_entries);
            kflags = (unsigned *)((char *)ring_ptr + off.flags);
            kdropped = (unsigned *)((char *)ring_ptr + off.dropped);
            array = (unsigned *)((char *)ring_ptr + off.array);
        }

        /**
         * @brief Sync internal state with kernel ring state on the SQ side.
         *
         * @return unsigned number of pending items in the SQ ring, for the
         * shared ring.
         */
        unsigned flush() noexcept {
            const unsigned mask = *kring_mask;
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

      public:
        template<unsigned URingFlags>
        friend class ::liburingcxx::URing;
        SubmissionQueue() noexcept = default;
        ~SubmissionQueue() noexcept = default;
    };

} // namespace detail

} // namespace liburingcxx