
#ifdef USE_MIMALLOC
#include <mimalloc-new-delete.h>
#endif
#include "co_context/config.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/detail/compat.hpp"
#include "co_context/detail/io_context_meta.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/io_context.hpp"
#include "co_context/log/log.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include "uring/uring_define.hpp"
#include <atomic>
#include <cassert>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

// fold level = 3 (ctrl+a, ctrl+k, ctrl+3 in vscode)
// unfold all (ctrl+a, ctrl+k, ctrl+j in vscode)

namespace co_context {

io_context::io_context() noexcept {
    auto &meta = detail::io_context_meta;
    std::lock_guard lg{meta.mtx};
    this->id = meta.create_count++;
    log::d(
        "&meta.create_count = %lx  value = %u\n", &meta.create_count,
        meta.create_count
    );
}

// Must be called by corresponding thread.
void io_context::init() {
    this->tid = ::gettid();
    detail::this_thread.ctx = this;
    detail::this_thread.ctx_id = this->id;
    this->worker.init(config::default_io_entries);
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
        if constexpr (config::submission_threshold != -1) {
            worker.check_submission_threshold();
        }
    }
}

void io_context::do_submission_part() noexcept {
    worker.poll_submission();
}

void io_context::do_completion_part() noexcept {
    // NOTE in the future: if an IO generates multiple requests_to_reap，
    // it must be counted carefully

    auto &meta = detail::io_context_meta;

    bool need_check_ring =
        (meta.ready_count > 1) | (worker.requests_to_reap > 0);

    if (need_check_ring) [[likely]] {
        uint32_t num = worker.poll_completion();

        // io_context will block itself here
        uint32_t will_not_wait =
            num | worker.has_task_ready() | worker.requests_to_submit;
        if (will_not_wait == 0) [[unlikely]] {
            num = worker.wait_and_poll_completion();
            if constexpr (config::is_log_i) {
                if (num == 0) [[unlikely]] {
                    log::i("wait_and_poll_completion() gets 0 cqe/event.\n");
                }
            }
        }
    } else if (!worker.has_task_ready()) [[unlikely]] {
        will_stop = true;
    }
}

void io_context::run() {
#ifdef CO_CONTEXT_USE_CPU_AFFINITY
#error TODO
    detail::set_cpu_affinity(detail::this_thread.ctx_id);
#endif
    log::i("io_context[%u] runs on %d\n", this->id, this->tid);

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
}

} // namespace co_context
