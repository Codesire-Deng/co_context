#include <co_context/all.hpp>
using namespace co_context;
using namespace std;

// unbuffered channel
channel<std::string> chan;

// buffered channel: more performace
channel<std::string, 8> perf_chan;

task<> produce(std::string tag) {
    constexpr int repeat = 2;
    for (;;) {
        for (int i = 0; i < repeat; ++i) {
            co_await chan.release(tag + ": fast produce");
        }
        for (int i = 0; i < repeat; ++i) {
            co_await timeout(1s);
            co_await chan.release(tag + ": slow produce");
        }
    }
}

task<> consume(std::string tag) {
    for (;;) {
        std::string str{co_await chan.acquire()};
        printf("%s: %s\n", tag.c_str(), str.c_str());
        co_await timeout(200ms);
    }
}

int main() {
    io_context ctx[6];
    ctx[0].co_spawn(produce("p0"));
    ctx[1].co_spawn(produce("p1"));
    ctx[2].co_spawn(produce("p2"));

    ctx[3].co_spawn(consume("c0"));
    ctx[4].co_spawn(consume("c1"));
    ctx[5].co_spawn(consume("c2"));

    for (auto &c : ctx) {
        c.start();
    }

    ctx[0].join();
    return 0;
}
