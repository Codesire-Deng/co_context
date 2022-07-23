#include "co_context/io_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/utility/buffer.hpp"
#include "co_context/eager_io.hpp"
#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>

using namespace co_context;

int times;
std::atomic_int finish = 0;
constexpr int concur = 2;

task<> workload() {
    int now = finish.fetch_add(1) + 1;
    if (now > times) { printf("logic error!\n"); }
    if (now == times) {
        printf("All done!\n");
        ::exit(0);
    }

    co_await eager::nop();
}

task<> gen_task() {
    for (int i = 0; i < times / concur; ++i) { co_spawn(workload()); }
    co_await eager::nop();
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n  %s times\n", argv[0]);
        return 0;
    }

    times = atoi(argv[1]);
    times -= times % concur;

    io_context context{32768};

    for (int i = 0; i < concur; ++i) context.co_spawn(gen_task());

    context.run();

    return 0;
}