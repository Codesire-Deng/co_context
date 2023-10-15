#include <co_context/all.hpp>
using namespace co_context;
using namespace std::chrono_literals;

using user_data_channel = co_context::channel<uint64_t, 4>;

task<> g(user_data_channel &ch) {
    printf("g() is running...\n");

    auto io = timeout(999s);
    co_await ch.release(io.user_data());

    int result = co_await io;

    if (result < 0) [[likely]] {
        printf("g() got error: %d %s\n", result, strerror(-result));
    } else {
        printf("g() got result: %d\n", result);
    }
}

task<> f() {
    user_data_channel ch;
    co_spawn(g(ch));

    uint64_t g_io_user_data = co_await ch.acquire();

    co_await timeout(1s);

    co_await cancel(g_io_user_data);
}

int main() {
    io_context ctx;
    ctx.co_spawn(f());
    ctx.start();
    ctx.join();
    return 0;
}

// Output:
// g() is running...
// g() got error: -125 Operation canceled
