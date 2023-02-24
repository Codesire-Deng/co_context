#include <chrono>
#include <iostream>
using namespace std;

int main() {
    auto t = chrono::steady_clock::now();
    t = chrono::steady_clock::now();
    auto t2 = chrono::steady_clock::now();
    auto d = t2 - t;
    cout << d.count() << endl;
    return 0;
}
