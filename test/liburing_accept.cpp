#include <assert.h>
#include <liburing.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int start_accept_listen(uint16_t port) {
    struct sockaddr_in addr;
    int fd;
    int ret;
    int optval;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    fd = socket(addr.sin_family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    assert(fd >= 0);
    optval = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
    assert(ret >= 0);

    ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(ret == 0);

    ret = listen(fd, SOMAXCONN);
    assert(ret == 0);

    printf("listen at 127.0.0.1:%hu\n", port);

    return fd;
}

int main() {
    struct io_uring_params p = {};
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct io_uring ring;

    int ret = io_uring_queue_init_params(8, &ring, &p);
    if (ret) {
        (void)fprintf(stderr, "ring setup failed: %d\n", ret);
        return 1;
    }

    int fd = start_accept_listen(1234);

    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, fd, NULL, NULL, 0);
    ret = io_uring_submit(&ring);
    if (ret != 1) {
        (void)fprintf(stderr, "submit: %d\n", ret);
        return 1;
    }

    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret) {
        fprintf(stderr, "wait: %d\n", ret);
        return 1;
    }

    return 0;
}
