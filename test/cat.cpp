/*
 *  Concatenate files and print on the standard output, using co_context.
 */

#include <co_context/lazy_io.hpp>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace co_context;

constexpr unsigned BLOCK_SZ = 1024;

constexpr int count_blocks(size_t size) noexcept {
    return int(size / BLOCK_SZ + ((size / BLOCK_SZ * BLOCK_SZ) != size));
}

void output(std::string_view s) noexcept {
    for (char c : s) {
        ::putchar(c);
    }
}

struct file_info final : std::vector<iovec> {
    ~file_info() noexcept {
        for (auto &iov : *this) {
            ::free(iov.iov_base);
        }
    }
};

// calculate the file size then malloc iovecs
file_info make_file_info(const size_t file_size) {
    const unsigned blocks = count_blocks(file_size);
    file_info fi;
    fi.reserve(blocks);

    // malloc buffers (to later let the ring fill them asynchronously)
    for (size_t rest = file_size; rest != 0;) {
        size_t to_read = std::min<size_t>(rest, BLOCK_SZ);
        iovec iov;
        iov.iov_len = to_read;
        if (posix_memalign(&iov.iov_base, BLOCK_SZ, BLOCK_SZ) != 0) {
            throw std::system_error{
                errno, std::system_category(), "posix_memalign"
            };
        }
        rest -= to_read;
        fi.push_back(iov);
    }

    return fi;
}

task<void> cat(const std::filesystem::path &path) {
    using std::cerr;
    try {
        // open the file
        int file_fd = ::open(path.c_str(), O_RDONLY);
        if (file_fd < 0) {
            throw std::system_error{errno, std::system_category(), "open"};
        }

        // calculate the file size then malloc iovecs
        auto fi = make_file_info(std::filesystem::file_size(path));

        // submit the read request
        co_await readv(file_fd, fi, 0);

        // output to console
        for (auto &iov : fi) {
            output({(const char *)iov.iov_base, iov.iov_len});
        }
    } catch (const std::system_error &e) {
        cerr << e.what() << "\n" << e.code() << "\n";
    } catch (const std::exception &e) {
        cerr << e.what() << '\n';
    }
}

task<> run(int n, char *files[]) {
    for (int i = 0; i < n; ++i) {
        printf("==%s\n", files[i]);
        co_await cat(files[i]);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [file name] <[file name] ...>\n", argv[0]);
        return 1;
    }

    io_context ctx;
    ctx.co_spawn(run(argc - 1, argv + 1));
    ctx.start();
    ctx.join();

    return 0;
}
