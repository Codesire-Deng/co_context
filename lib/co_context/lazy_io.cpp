#include "co_context/lazy_io.hpp"

namespace co_context {

namespace detail {

    extern thread_local thread_meta this_thread;

    // void lazy_awaiter::await_suspend(std::coroutine_handle<> current)
    // noexcept {
    //     io_info.handle = current;
    //     assert(detail::this_thread.worker != nullptr);
    //     worker_meta &worker = *detail::this_thread.worker;
    //     worker.submit(&io_info);
    // }

} // namespace detail

namespace lazy {} // namespace lazy

} // namespace co_context
