#pragma once

#include "co_context/co/condition_variable.hpp"
#include "co_context/co/semaphore.hpp"

namespace co_context {
template<typename T, size_t capacity = 0>
class channel {
    static_assert(capacity != 0);

  public:
    task<T> acquire() {
        co_await mtx.lock();
        co_await not_empty_cv.wait(mtx, [this] { return !this->buf.empty(); });
        auto &&front = std::move(buf.front());
        buf.pop();
        mtx.unlock();
        not_full_cv.notify_one();
        co_return std::move(front);
    }

    task<> release(const T &x) {
        co_await mtx.lock();
        co_await not_full_cv.wait(mtx, [this] {
            return this->buf.size() != capacity;
        });
        buf.push(x);
        mtx.unlock();
        not_empty_cv.notify_one();
        co_return;
    }

    task<> release(T &&x) {
        co_await mtx.lock();
        co_await not_full_cv.wait(mtx, [this] {
            return this->buf.size() != capacity;
        });
        buf.push(std::forward(x));
        mtx.unlock();
        not_empty_cv.notify_one();
        co_return;
    }

  private:
    std::queue<T> buf;
    condition_variable not_full_cv;
    condition_variable not_empty_cv;
    mutex mtx;
};

template<typename T>
class channel<T, 0> {
  public:
    static constexpr int x = 0;
};

} // namespace co_context
