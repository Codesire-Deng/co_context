#include <mimalloc-new-delete.h>
#include "co_context.hpp"
#include "co_context/net/acceptor.hpp"
#include "co_context/buffer.hpp"

#include <filesystem>
#include <random>
#include <fcntl.h>
#include <atomic>
#include <chrono>

using namespace co_context;

int times;
std::atomic_int finish = 0;
constexpr int concur = 3;

main_task run() {
    if (finish.fetch_add(1) + 1 == times) {
        printf("All done!\n");
        ::exit(0);
    }
    co_await eager::nop();
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n  %s times\n", argv[0]);
        return 0;
    }

    times = atoi(argv[1]);
    times -= times % concur;

    io_context context{256};

    for (int i = 0; i < concur; ++i)
        context.co_spawn([]() -> main_task {
            for (int i = 0; i < times / concur; ++i) { co_spawn(run()); }
            co_await eager::nop();
        }());

    context.run();

    return 0;
}