#pragma once

#include "../io_uring.h"
#include <cstdint>
#include <span>
#include <sys/socket.h>

namespace liburingcxx {

static inline io_uring_recvmsg_out *
recvmsg_validate(std::span<char> buf, msghdr *msgh) noexcept {
    const size_t header =
        msgh->msg_controllen + msgh->msg_namelen + sizeof(io_uring_recvmsg_out);
    if (buf.size() < header) {
        return nullptr;
    }
    return reinterpret_cast<io_uring_recvmsg_out *>(buf.data());
}

static inline void *recvmsg_name(io_uring_recvmsg_out *o) {
    return (void *)&o[1];
}

static inline cmsghdr *
recvmsg_cmsg_firsthdr(io_uring_recvmsg_out *o, msghdr *msgh) {
    if (o->controllen < sizeof(cmsghdr)) {
        return nullptr;
    }

    return (cmsghdr *)((unsigned char *)recvmsg_name(o) + msgh->msg_namelen);
}

static inline cmsghdr *
recvmsg_cmsg_nexthdr(io_uring_recvmsg_out *o, msghdr *msgh, cmsghdr *cmsg) {
    unsigned char *end;

    if (cmsg->cmsg_len < sizeof(cmsghdr)) {
        return nullptr;
    }
    end = (unsigned char *)recvmsg_cmsg_firsthdr(o, msgh) + o->controllen;
    cmsg = (cmsghdr *)((unsigned char *)cmsg + CMSG_ALIGN(cmsg->cmsg_len));

    if ((unsigned char *)(cmsg + 1) > end) {
        return nullptr;
    }
    if (((unsigned char *)cmsg) + CMSG_ALIGN(cmsg->cmsg_len) > end) {
        return nullptr;
    }

    return cmsg;
}

static inline void *recvmsg_payload(io_uring_recvmsg_out *o, msghdr *msgh) {
    // clang-format off
    return (void *)((unsigned char *)recvmsg_name(o)
            + msgh->msg_namelen + msgh->msg_controllen);
    // clang-format on
}

static inline unsigned int
recvmsg_payload_length(io_uring_recvmsg_out *o, int buf_len, msghdr *msgh) {
    uint64_t payload_start, payload_end;

    payload_start = (uint64_t)recvmsg_payload(o, msgh);
    payload_end = (uint64_t)o + buf_len;
    return (unsigned int)(payload_end - payload_start);
}

} // namespace liburingcxx
