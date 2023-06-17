#include <co_context/all.hpp>
using namespace co_context;
using namespace std::chrono_literals;

task<> g(stop_token token) {
    printf("g() is running...\n");

    stop_callback guard{token, [] {
                            printf("Callback is triggerd\n");
                        }};

    co_await timeout(1s);

    // It is your responsibility to check
    // whether the task has been cancelled.
    if (token.stop_requested()) {
        printf("g() exits early\n");
        co_return;
    }

    printf("g() finished\n");
}

task<> f() {
    // The same as std::stop_source.
    // See https://en.cppreference.com/w/cpp/header/stop_token
    stop_source source;
    stop_token token = source.get_token();

    co_spawn(g(token));

    co_await yield();      // Let `g` starts.
    source.request_stop(); // Trigger the callback.

    // The `source` and `token` are destroyed before `g` finished.
    // It is safe to do that.
    co_return;
}

int main() {
    io_context ctx;
    ctx.co_spawn(f());
    ctx.start();
    ctx.join();
    return 0;
}
