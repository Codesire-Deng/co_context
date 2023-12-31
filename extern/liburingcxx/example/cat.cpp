/*
 *  A tester using liburingcxx.
 *
 *  Copyright (C) 2022 Zifeng Deng
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <uring/uring.hpp>

#include <fcntl.h>

#include <filesystem>
#include <iostream>

constexpr unsigned BLOCK_SZ = 1024;

struct file_info {
    size_t size;
    iovec iovecs[];
};

constexpr int count_blocks(size_t size, unsigned block_sz) noexcept {
    return size / block_sz + ((size / block_sz * block_sz) != size);
}

void output(std::string_view s) noexcept {
    for (char c : s) {
        putchar(c);
    }
}

using uring = liburingcxx::uring<0>;

void submit_read_request(uring &ring, const std::filesystem::path path) {
    // open the file
    int file_fd = open(path.c_str(), O_RDONLY);
    if (file_fd < 0) {
        throw std::system_error{errno, std::system_category(), "open"};
    }

    // calculate the file size then malloc iovecs
    const size_t file_size = std::filesystem::file_size(path);
    const unsigned blocks = count_blocks(file_size, BLOCK_SZ);
    file_info *fi = (file_info *)malloc(sizeof(*fi) + (blocks * sizeof(iovec)));
    fi->size = file_size;

    // malloc buffers (to later let the ring fill them asynchronously)
    for (size_t rest = file_size, i = 0; rest != 0; ++i) {
        size_t to_read = std::min<size_t>(rest, BLOCK_SZ);
        fi->iovecs[i].iov_len = to_read;
        if (posix_memalign(&fi->iovecs[i].iov_base, BLOCK_SZ, BLOCK_SZ) != 0) {
            throw std::system_error{
                errno, std::system_category(), "posix_memalign"
            };
        }
        rest -= to_read;
    }

    // submit the read request
    liburingcxx::sq_entry &sqe = *ring.get_sq_entry();
    sqe.prep_readv(file_fd, std::span{fi->iovecs, blocks}, 0)
        .set_data(reinterpret_cast<uint64_t>(fi));

    // Must be called after any request (except for polling mode)
    ring.append_sq_entry(&sqe);
    ring.submit();
}

void wait_result_and_print(uring &ring) {
    // get a result from the ring
    const liburingcxx::cq_entry *cqe;
    [[maybe_unused]] int err = ring.wait_cq_entry(cqe);

    // get the according data
    file_info *fi = reinterpret_cast<file_info *>(cqe->user_data);

    // print the data to console
    const int blocks = count_blocks(fi->size, BLOCK_SZ);
    for (int i = 0; i < blocks; ++i) {
        output({(char *)fi->iovecs[i].iov_base, fi->iovecs[i].iov_len});
    }

    // Must be called after consuming a cqe
    ring.seen_cq_entry(cqe);
}

int main(int argc, char *argv[]) {
    using std::cout, std::endl, std::cerr;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [file name] <[file name] ...>\n", argv[0]);
        return 1;
    }

    uring ring;
    ring.init(8);

    for (int i = 1; i < argc; ++i) {
        try {
            // put read request into the ring
            submit_read_request(ring, argv[i]);
            // get string from the ring, and output to console
            wait_result_and_print(ring);
        } catch (const std::system_error &e) {
            cerr << e.what() << "\n" << e.code() << "\n";
        } catch (const std::exception &e) {
            cerr << e.what() << '\n';
        }
    }

    return 0;
}
