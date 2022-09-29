#pragma once

#include <numeric>
#include "uring/sq_entry.hpp"

namespace liburingcxx {

template<unsigned uring_flags>
class uring;

namespace detail {

    using sq_entry = ::liburingcxx::sq_entry;

    static_assert(sizeof(sq_entry) == sizeof(io_uring_sqe));
    static_assert(sizeof(sq_entry) == 64);
    static_assert(alignof(sq_entry) == 8);

    class submission_queue final {
      private:
        unsigned sqe_head;      // memset to 0 during uring()
        unsigned sqe_tail;      // memset to 0 during uring()
        unsigned sqe_free_head; // memset to 0 during uring()

        unsigned *khead;
        unsigned *ktail;
        unsigned ring_mask;
        unsigned ring_entries;
        unsigned *kflags;
        unsigned *kdropped;
        unsigned *array;
        sq_entry *sqes;
        size_t ring_sz;
        void *ring_ptr;

      private:
        void set_offset(const io_sqring_offsets &off) noexcept {
            khead = (unsigned *)((uintptr_t)ring_ptr + off.head);
            ktail = (unsigned *)((uintptr_t)ring_ptr + off.tail);
            ring_mask = *(unsigned *)((uintptr_t)ring_ptr + off.ring_mask);
            ring_entries =
                *(unsigned *)((uintptr_t)ring_ptr + off.ring_entries);
            kflags = (unsigned *)((uintptr_t)ring_ptr + off.flags);
            kdropped = (unsigned *)((uintptr_t)ring_ptr + off.dropped);
            array = (unsigned *)((uintptr_t)ring_ptr + off.array);
        }

        void init_free_queue() noexcept {
            std::iota(array, array + ring_entries, 0);
        }

        /**
         * @brief Sync internal state with kernel ring state on the SQ side.
         *
         * @return unsigned number of pending items in the SQ ring, for the
         * shared ring.
         */
        unsigned flush() noexcept {
            if (sqe_tail != sqe_head) [[likely]] {
                /*
                 * Fill in sqes that we have queued up, adding them to the
                 * kernel ring
                 */
                sqe_head = sqe_tail;
                /*
                 * Ensure that the kernel sees the SQE updates before it sees
                 * the tail update.
                 */
                io_uring_smp_store_release(ktail, sqe_tail);
            }
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
            return sqe_tail - *khead;
        }

        [[nodiscard]] inline sq_entry *get_sq_entry() noexcept {
            const unsigned int head = io_uring_smp_load_acquire(khead);
            if (sqe_free_head - head < ring_entries) [[likely]] {
                return &sqes[array[sqe_free_head++ & ring_mask]];
            } else {
                return nullptr;
            }
        }

        inline void append_sq_entry(const sq_entry *const sqe) noexcept {
            array[sqe_tail++ & ring_mask] = sqe - sqes;
            assert(sqe_tail - *khead <= ring_entries);
        }

      public:
        template<unsigned uring_flags>
        friend class ::liburingcxx::uring;
        submission_queue() noexcept = default;
        ~submission_queue() noexcept = default;
    };

    // char (*____)[sizeof(submission_queue)] = 1;

    static_assert(sizeof(submission_queue) == 88);

} // namespace detail

} // namespace liburingcxx
