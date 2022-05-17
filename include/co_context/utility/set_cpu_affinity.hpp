#pragma once

#ifdef CO_CONTEXT_USE_CPU_AFFINITY // see config.hpp

#include <sched.h>

namespace co_context {

namespace detail {

    /**
     * @brief Set the cpu affinity for the caller thread.
     * @param cpu
     * @note using `sched_setaffinity` with the std::thread might be an
     * undefined behavior according to ... according to what ??? (forgetful)
     */
    inline void set_cpu_affinity(int cpu) {
        ::cpu_set_t cpu_set;
        CPU_SET(cpu, &cpu_set);
        int ret = sched_setaffinity(gettid(), sizeof(cpu_set_t), &cpu_set);
        if (ret != 0) [[unlikely]]
            throw std::system_error{
                errno, std::system_category(), "sched_setaffinity"};
    }

} // namespace detail

} // namespace co_context

#endif // ifdef CO_CONTEXT_USE_CPU_AFFINITY
