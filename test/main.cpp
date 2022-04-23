// #include <mimalloc-new-delete.h>
#include "co_context/io_context.hpp"
#include "co_context/lazy_io.hpp"
#include "co_context/net/socket.hpp"
#include "co_context/net/acceptor.hpp"
#include <filesystem>
#include <fcntl.h>
#include <iostream>

int main(int argc, char *argv[]) {
    using std::cout, std::endl, std::cerr;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s [file name] <[file name] ...>\n", argv[0]);
        return 1;
    }

    using namespace co_context;
    io_context io_context{8};

    // io_context.probe();

    int output_fd = ::open("output", O_WRONLY | O_TRUNC);

    io_context.co_spawn([&]() -> task<>  {
        constexpr int buf_size = 4096 * 2;
        char buf[buf_size];

        for (int i = 1; i < argc; ++i) {
            const std::filesystem::path path{argv[i]};
            int file_fd = ::open(path.c_str(), O_RDONLY);
            if (file_fd < 0) {
                throw std::system_error{errno, std::system_category(), "open"};
            }

            // calculate the file size then malloc iovecs
            const size_t file_size = std::filesystem::file_size(path);

            for (size_t offset = 0; offset < file_size;) {
                const size_t to_read =
                    std::min<size_t>(buf_size, file_size - offset);
                int res = ::read(file_fd, buf, buf_size);
                [[maybe_unused]] int nr = ::write(output_fd, buf, res);
                // int res = co_await lazy::read(file_fd, buf, offset);
                // co_await lazy::write(output_fd, {buf, res}, offset);
                offset += res;
                // for (int i=0; i<res; ++i)
                //     putchar(buf[i]);
            }
        }
        printf("ok\n");
        exit(0);
    }());

    io_context.run();

    return 0;
}