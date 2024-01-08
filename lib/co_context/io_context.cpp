#include <co_context/config/config.hpp>
#if CO_CONTEXT_USE_MIMALLOC
#include <mimalloc-new-delete.h>
#endif

#include <co_context/co/semaphore.hpp>
#include <co_context/config/io_context.hpp>
#include <co_context/detail/io_context_meta.hpp>
#include <co_context/detail/thread_meta.hpp>
#include <co_context/io_context.hpp>
#include <co_context/log/log.hpp>

#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <exception>
#include <mutex>
#include <thread>

namespace co_context {

// Must be called by corresponding thread.
void io_context::init() {
    detail::this_thread.ctx = this;
    detail::this_thread.ctx_id = this->id;

    this->worker.init(config::default_io_uring_entries);
}

// Must be called by corresponding thread.
void io_context::deinit() noexcept {
    detail::this_thread.ctx = nullptr;
    detail::this_thread.ctx_id = static_cast<config::ctx_id_t>(-1);

    this->worker.deinit();

    auto &meta = detail::io_context_meta;
    std::lock_guard lg{meta.mtx};
    --meta.ready_count;
    --meta.create_count;
    log::d(
        "io_context::deinit(): &meta.create_count = %lx  value = %u\n",
        &meta.create_count, meta.create_count
    );
}

void io_context::start() {
    host_thread = std::thread{[this] {
        this->init();
        auto &meta = detail::io_context_meta;
        {
            std::unique_lock lock{meta.mtx};
            ++meta.ready_count;
            log::d(
                "io_context[%u] ready. (%u/%u)\n", this->id, meta.create_count,
                meta.ready_count
            );
            if (!meta.cv.wait_for(lock, std::chrono::seconds{1}, [] {
                    return meta.create_count == meta.ready_count;
                })) {
                log::e("io_context initialization timeout. There exists an "
                       "io_context that has not been started.\n");
                std::terminate();
            }
        }
        meta.cv.notify_all();

        // All io_context gets its uring ready from now.

        this->run();
    }};
}

void io_context::do_worker_part() {
    auto num = worker.number_to_schedule();
    log::v("worker[%u] will run %u times...\n", id, num);
    for (; num > 0; --num) {
        worker.work_once();
        if constexpr (config::submission_threshold != -1U) {
            worker.check_submission_threshold();
        }
    }
}

void io_context::do_submission_part() noexcept {
    worker.poll_submission();
}

void io_context::do_completion_part_bad_path() noexcept {
    log::v("do_completion_part_bad_path(): bad path\n");
    const auto &meta = detail::io_context_meta;
    if (!worker.peek_uring()
        && (worker.requests_to_reap > 0 || meta.ready_count > 1)) {
        log::v("do_completion_part_bad_path(): block on worker.wait_uring()\n");
        worker.wait_uring();
    }
    const uint32_t handled_num = worker.poll_completion();

    bool is_not_over =
        handled_num | (meta.ready_count > 1) | worker.requests_to_reap;

    if (!is_not_over) [[unlikely]] {
        will_stop = true;
    }
}

void io_context::do_completion_part() noexcept {
    // NOTE in the future: if an IO generates multiple requests_to_reap，
    // it must be counted carefully

    const uint32_t handled_num =
        worker.peek_uring() ? worker.poll_completion() : 0;
    bool is_fast_path =
        worker.requests_to_submit | worker.has_task_ready() | handled_num;
    if (is_fast_path) [[likely]] {
        return;
    }

    do_completion_part_bad_path();
}

void io_context::run() {
    log::i(
        "io_context[%u] runs on %lx\n", this->id,
        static_cast<uintptr_t>(this->host_thread.native_handle())
    );

#if CO_CONTEXT_IS_USING_EVENTFD
    auto &meta = detail::io_context_meta;
    if (meta.create_count >= 2) {
        worker.listen_on_co_spawn();
    }
#endif

    while (!will_stop) [[likely]] {
        do_worker_part();

        do_submission_part();

        do_completion_part();
    }

    log::d("io_context[%u] stopped\n", this->id);

    deinit();
}

} // namespace co_context
