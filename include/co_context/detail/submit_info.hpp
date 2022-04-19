#pragma once

namespace liburingcxx {

class SQEntry;

} // namespace liburingcxx

namespace co_context {

namespace detail {

    struct task_info;

    union submit_info {
        task_info *request;
        liburingcxx::SQEntry *sqe;
    };

} // namespace detail

} // namespace co_context
