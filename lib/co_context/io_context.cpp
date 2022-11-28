#include <cassert>
#ifdef USE_MIMALLOC
#include <mimalloc-new-delete.h>
#endif
#include "co_context/config.hpp"
#include "co_context/co/condition_variable.hpp"
#include "co_context/co/semaphore.hpp"
#include "co_context/compat.hpp"
#include "co_context/detail/thread_meta.hpp"
#include "co_context/io_context.hpp"
#include "co_context/log/log.hpp"
#include "co_context/utility/set_cpu_affinity.hpp"
#include "uring/uring_define.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

// fold level = 3 (ctrl+a, ctrl+k, ctrl+3 in vscode)
// unfold all (ctrl+a, ctrl+k, ctrl+j in vscode)

namespace co_context {

detail::io_context_meta io_context::meta;

void io_context::start() {
    host_thread = std::thread{[this] {
        this->tid = gettid();
        this->init();

        {
            std::unique_lock lock{meta.mtx};
            ++meta.ready_count;
            log::d(
                "io_context[%u] ready. (%u/%u)\n", this->id,
                io_context::meta.create_count, io_context::meta.ready_count
            );
            if (!meta.cv.wait_for(lock, std::chrono::seconds{1}, [] {
                    return io_context::meta.create_count
                           == io_context::meta.ready_count;
                })) {
                log::e("io_context initialization timeout. There exists an "
                       "io_context that has not been started.\n");
                std::exit(1);
            }
        }
        meta.cv.notify_all();

        // All io_context gets its uring ready from now.

        this->run();
    }};
}

void io_context::run() {
#ifdef CO_CONTEXT_USE_CPU_AFFINITY
#error TODO
    detail::set_cpu_affinity(detail::this_thread.ctx_id);
#endif
    log::i("io_context[%u] runs on %d\n", this->id, this->tid);

#if CO_CONTEXT_IS_USING_EVENTFD
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
