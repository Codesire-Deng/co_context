#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

int times;
std::atomic_int finish = 0;
constexpr int concur = 2;

void workload() {
    for (int i = 0; i < times / concur; ++i) {
        int now = finish.fetch_add(1) + 1;
        if (now == times) {
            printf("All done!\n");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n  %s times\n", argv[0]);
        return 0;
    }

    times = atoi(argv[1]);
    times -= times % concur;

    std::vector<std::jthread> v;
    for (int i = 0; i < concur; ++i) {
        v.emplace_back(workload);
    }

    return 0;
}