#include <co_context/co/semaphore.hpp>
#include <co_context/detail/thread_meta.hpp>
#include <co_context/io_context.hpp>
#include <co_context/log/log.hpp>

#include <atomic>
#include <cassert>
#include <coroutine>

namespace co_context {

counting_semaphore::~counting_semaphore() noexcept {
    if constexpr (config::is_log_d) {
        if (awaiting.load(std::memory_order_relaxed) != nullptr
            || to_resume != nullptr) {
            log::d("[WARNING] ~counting_semaphore(): coroutine leak\n");
        }
    }
}

bool counting_semaphore::try_acquire() noexcept {
    T old_counter = counter.load(std::memory_order_relaxed);
    return old_counter > 0
           && counter.compare_exchange_strong(
               old_counter, old_counter - 1, std::memory_order_acquire,
               std::memory_order_relaxed
           );
}

void counting_semaphore::release() noexcept {
    constexpr T update = 1;
    // register semaphore-update event, to io_context(worker)
    const T old_counter = counter.fetch_add(update, std::memory_order_release);
    if (old_counter >= 0) {
        return;
    }

    notifier_mtx.lock();
    acquire_awaiter *awaken_awaiter = try_release();
    notifier_mtx.unlock();

    awaken_awaiter->co_spawn();
};

void counting_semaphore::release(T update) noexcept {
    // register semaphore-update event, to io_context(worker)
    const T old_counter = counter.fetch_add(update, std::memory_order_release);
    if (old_counter >= 0) {
        return;
    }

    update = std::max(old_counter, -update);
    {
        notifier_mtx.lock();
        do {
            acquire_awaiter *awaken_awaiter = try_release();
            awaken_awaiter->co_spawn();
        } while (++update < 0);
        notifier_mtx.unlock();
    }
};

counting_semaphore::acquire_awaiter *
counting_semaphore::try_release() noexcept {
    acquire_awaiter *resume_head = to_resume;
    if (resume_head == nullptr) [[unlikely]] {
        auto *node = awaiting.exchange(nullptr, std::memory_order_acquire);
        if (node == nullptr) [[unlikely]] {
            return nullptr; // no awaiting
        }

        do {
            acquire_awaiter *tmp = node->next;
            node->next = resume_head;
            resume_head = node;
            node = tmp;
        } while (node != nullptr);
    }

    assert(resume_head != nullptr);

    to_resume = resume_head->next;
    return resume_head;
}

void counting_semaphore::acquire_awaiter::await_suspend(
    std::coroutine_handle<> current
) noexcept {
    this->handle = current;
    log::v("suspending coro: %lx\n", this->handle.address());
    acquire_awaiter *old_head = sem.awaiting.load(std::memory_order_relaxed);
    do {
        this->next = old_head;
    } while (!sem.awaiting.compare_exchange_weak(
        old_head, this, std::memory_order_release, std::memory_order_relaxed
    ));
}

void counting_semaphore::acquire_awaiter::co_spawn() const noexcept {
    this->resume_ctx->worker.co_spawn_auto(this->handle);
}

} // namespace co_context
