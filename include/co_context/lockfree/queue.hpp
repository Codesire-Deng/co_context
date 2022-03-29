#pragma once

#include <type_traits>
#include <atomic>

namespace co_context {

template<typename T>
    requires std::is_scalar_v<T>
struct forward_list_node {
    forward_list_node *next;
    T val;
};

template<typename T>
    requires std::is_scalar_v<T>
class mpsc_queue {
  public:
    mpsc_queue() noexcept : head(nullptr), tail(nullptr) {}

    bool empty() const noexcept { return head == nullptr; }

    void push(T value) noexcept;

    void pop() noexcept;

    T front() noexcept;

  private:
    forward_list_node *head;
    std::atomic<forward_list_node *> tail;
};

} // namespace co_context
