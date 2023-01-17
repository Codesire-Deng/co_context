#pragma once

#include "co_context/config.hpp"
#include <bits/iterator_concepts.h>
#include <boost/circular_buffer.hpp>
#include <cassert>
#include <concepts>
#include <iterator>
#include <memory>
#include <type_traits>

#define CO_CONTEXT_CB_ASSERT(expr)    \
    if constexpr (config::is_log_d) { \
        assert(expr);                 \
    }

namespace co_context::detail::cb {

auto uninitialized_move_if_noexcept(
    std::input_iterator auto first,
    std::input_iterator auto last,
    std::forward_iterator auto dest
) {
    using value_type = decltype(dest)::value_type;
    using tag_t = std::is_nothrow_move_constructible<value_type>;
    if constexpr (tag_t::value) {
        return std::uninitialized_move(first, last, dest);
    } else {
        return std::uninitialized_copy(first, last, dest);
    }
}

template<class Alloc>
struct nonconst_traits : std::allocator_traits<Alloc> {
    using reference = std::allocator_traits<Alloc>::value_type &;
    using nonconst_self = nonconst_traits<Alloc>;
};

template<class Alloc>
struct const_traits : std::allocator_traits<Alloc> {
    using reference = const std::allocator_traits<Alloc>::value_type &;
    using pointer = std::allocator_traits<Alloc>::const_pointer;
    using void_pointer = std::allocator_traits<Alloc>::const_void_pointer;
    using nonconst_self = nonconst_traits<Alloc>;
};

template<typename Buff, typename Traits>
class iterator {
  public:
    using this_type = iterator<Buff, Traits>;
    using nonconst_self = iterator<Buff, typename Traits::nonconst_self>;
    using iterator_category = std::random_access_iterator_tag;
    using value_type = Traits::value_type;
    using pointer = Traits::pointer;
    using reference = Traits::reference;
    using size_type = Traits::size_type;
    using difference_type = Traits::difference_type;

    static_assert(std::random_access_iterator<this_type>);

  private:
    const Buff *m_buff;

    pointer m_it;

    constexpr void assert_buff_valid() const noexcept {
        CO_CONTEXT_CB_ASSERT(m_buff != nullptr);
    }

    constexpr void assert_valid() const noexcept {
        CO_CONTEXT_CB_ASSERT(m_buff != nullptr);
        CO_CONTEXT_CB_ASSERT(m_it != nullptr);
    }

    pointer linearize_pointer() const noexcept {
        if (m_it == nullptr) {
            return m_buff->m_start + m_buff->size();
        }
        if (m_it < m_buff->m_first) {
            return m_it + (m_buff->m_end - m_buff->m_first);
        }
        return m_buff->m_start + (m_it - m_buff->m_first);
    }

  public:
    iterator() noexcept = default;

    explicit iterator(const nonconst_self &it) noexcept
        : m_buff(it.m_buff)
        , m_it(it.m_it) {}

    iterator(const Buff *cb, pointer p) noexcept : m_buff(cb), m_it(p) {}

    iterator &operator=(const iterator &it) {
        if (this == &it) {
            return *this;
        }
        m_buff = it.m_buff;
        m_it = it.m_it;
        return *this;
    }

    reference operator*() const {
        assert_valid();
        return *m_it;
    }

    pointer operator->() const {
        assert_valid();
        return m_it;
    }

    template<typename Traits0>
    difference_type operator-(const iterator<Buff, Traits0> &it
    ) const noexcept {
        assert_buff_valid();
        it.assert_buff_valid();
        return linearize_pointer() - it.linearize_pointer();
    }

    // Increment operator (prefix).
    iterator &operator++() noexcept {
        assert_valid();
        m_buff->increase(m_it);
        if (m_it == m_buff->m_last) [[unlikely]] {
            m_it = 0;
        }
        return *this;
    }

    // Increment operator (postfix).
    iterator operator++(int) noexcept {
        iterator tmp{*this};
        ++*this;
        return tmp;
    }

    // Decrement operator (prefix).
    iterator &operator--() noexcept {
        assert_buff_valid();
        // check for iterator pointing to begin()
        CO_CONTEXT_CB_ASSERT(m_it != m_buff->m_first);

        if (m_it == 0) {
            m_it = m_buff->m_last;
        }
        m_buff->decrease(m_it);
        return *this;
    }

    // Decrement operator (postfix).
    iterator operator--(int) noexcept {
        iterator tmp{*this};
        --*this;
        return tmp;
    }

    // Iterator addition.
    iterator &operator+=(difference_type n) noexcept {
        assert_buff_valid();
        if (n > 0) [[likely]] {
            // check for too large n
            CO_CONTEXT_CB_ASSERT(m_buff->end() - *this >= n);

            m_it = m_buff->add(m_it, n);
            if (m_it == m_buff->m_last) {
                m_it = 0;
            }
        } else if (n < 0) {
            *this -= -n;
        }
        return *this;
    }

    // Iterator addition.
    iterator operator+(difference_type n) const noexcept {
        return iterator{*this} += n;
    }

    // Iterator subtraction.
    iterator &operator-=(difference_type n) noexcept {
        assert_buff_valid();
        if (n > 0) [[likely]] {
            // check for too large n
            CO_CONTEXT_CB_ASSERT(*this - m_buff->begin() >= n);

            m_it = m_buff->sub(m_it == 0 ? m_buff->m_last : m_it, n);
        } else if (n < 0) {
            *this += -n;
        }
        return *this;
    }

    // Iterator subtraction.
    iterator operator-(difference_type n) const noexcept {
        return iterator(*this) -= n;
    }

    // Element access operator.
    reference operator[](difference_type n) const noexcept {
        return *(*this + n);
    }

    template<typename Traits0>
    bool operator==(const iterator<Buff, Traits0> &it) const noexcept {
        assert_buff_valid();
        it.assert_buff_valid();
        CO_CONTEXT_CB_ASSERT(!(m_it == it.m_it && m_buff != it.m_buff));

        return m_it == it.m_it;
    }

    template<typename Traits0>
    bool operator!=(const iterator<Buff, Traits0> &it) const noexcept {
        return !this->operator==(it);
    }

    // Less.
    template<class Traits0>
    bool operator<(const iterator<Buff, Traits0> &it) const noexcept {
        assert_buff_valid();
        it.assert_buff_valid();
        return linearize_pointer() < it.linearize_pointer();
    }

    // Greater.
    template<class Traits0>
    bool operator>(const iterator<Buff, Traits0> &it) const noexcept {
        return it < *this;
    }

    // Less or equal.
    template<class Traits0>
    bool operator<=(const iterator<Buff, Traits0> &it) const noexcept {
        return !(it < *this);
    }

    // Greater or equal.
    template<class Traits0>
    bool operator>=(const iterator<Buff, Traits0> &it) const noexcept {
        return !(*this < it);
    }
};

} // namespace co_context::detail::cb

namespace co_context {

template<typename T, typename Alloc = std::allocator<T>>
class circular_buffer {
  public:
    using this_type = circular_buffer<T, Alloc>;
    using value_type = Alloc::value_type;
    using pointer = std::allocator_traits<Alloc>::pointer;
    using reference = value_type &;
    using const_reference = const value_type &;
    using difference_type = std::allocator_traits<Alloc>::difference_type;
    using size_type = std::allocator_traits<Alloc>::size_type;
    using allocator_type = Alloc;
    using capacity_type = size_type;

    using const_iterator =
        detail::cb::iterator<this_type, detail::cb::const_traits<Alloc>>;

    using iterator =
        detail::cb::iterator<this_type, detail::cb::nonconst_traits<Alloc>>;

    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    using reverse_iterator = std::reverse_iterator<iterator>;

  private:
    pointer m_start;
    pointer m_end;
    pointer m_first;
    pointer m_last;
    size_type m_size;

    template<typename Buff, typename Traits>
    friend class detail::cb::iterator;

  public:
    size_type size() const noexcept { return m_size; }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    [[nodiscard]] bool full() const noexcept { return capacity() == size(); }

    capacity_type capacity() const noexcept { return m_end - m_start; }

    void set_capacity(capacity_type new_capacity) {
        if (new_capacity == capacity()) [[unlikely]] {
            return;
        }
        pointer start = Alloc{}.allocate(new_capacity);
        iterator b = begin();
        if constexpr (noexcept(reset(
                          start,
                          detail::cb::uninitialized_move_if_noexcept(
                              b, b + 1, start
                          ),
                          new_capacity
                      ))) {
            reset(start, b + std::min(new_capacity, size()), new_capacity);
        } else {
            try {
                reset(start, b + std::min(new_capacity, size()), new_capacity);
            } catch (...) {
                deallocate(start, new_capacity);
                throw;
            }
        }
    }

    void resize(size_type new_size, const value_type &item = value_type{}) {
        if (new_size > size()) {
            if (new_size > capacity()) {
                set_capacity(new_size);
            }
            insert(end(), new_size - size(), item);
        } else {
            iterator e = end();
            erase(e - (size() - new_size), e);
        }
    }

    // TODO: insert
    // TODO: erase

    iterator begin() noexcept { return iterator{this, empty() ? 0 : m_first}; }

    iterator end() noexcept { return iterator{this, 0}; }

    const_iterator begin() const noexcept {
        return const_iterator{this, empty() ? 0 : m_first};
    }

    const_iterator cbegin() const noexcept { return begin(); }

    const_iterator end() const noexcept { return const_iterator{this, 0}; }

    const_iterator cend() const noexcept { return end(); }

    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

    const_reverse_iterator rbegin() const noexcept {
        return const_reverse_iterator(end());
    }

    const_reverse_iterator rend() const noexcept {
        return const_reverse_iterator(begin());
    }

    reference operator[](size_type index) noexcept {
        CO_CONTEXT_CB_ASSERT(index < size()); // check for invalid index
        return *add(m_first, index);
    }

    const_reference operator[](size_type index) const noexcept {
        CO_CONTEXT_CB_ASSERT(index < size()); // check for invalid index
        return *add(m_first, index);
    }

    reference at(size_type index) {
        check_position(index);
        return (*this)[index];
    }

    const_reference at(size_type index) const {
        check_position(index);
        return (*this)[index];
    }

    reference front() noexcept {
        // check for empty buffer (front element not available)
        CO_CONTEXT_CB_ASSERT(!empty());
        return *m_first;
    }

    const_reference front() const noexcept {
        // check for empty buffer (front element not available)
        CO_CONTEXT_CB_ASSERT(!empty());
        return *m_first;
    }

    reference back() noexcept {
        // check for empty buffer (back element not available)
        CO_CONTEXT_CB_ASSERT(!empty());
        return *((m_last == m_start ? m_end : m_last) - 1);
    }

    const_reference back() const noexcept {
        // check for empty buffer (back element not available)
        CO_CONTEXT_CB_ASSERT(!empty());
        return *((m_last == m_start ? m_end : m_last) - 1);
    }

    [[nodiscard]] bool is_linearized() const noexcept {
        return m_first < m_last || m_last == m_start;
    }

  private:
    void check_position(size_type index) const {
        if (index >= size()) [[unlikely]] {
            throw std::out_of_range{"circular_buffer"};
        }
    }

    template<typename U>
    static constexpr bool is_raw_pointer_v =
        std::is_same_v<U, pointer>
        || std::is_same_v<U, typename const_iterator::pointer>;

    template<typename Pointer>
        requires is_raw_pointer_v<Pointer>
    void increase(Pointer &p) const {
        ++p;
        if (p == m_end) [[unlikely]] {
            p = m_start;
        }
    }

    template<typename Pointer>
        requires is_raw_pointer_v<Pointer>
    void decrease(Pointer &p) const {
        if (p == m_start) [[unlikely]] {
            p = m_end;
        }
        --p;
    }

    template<typename Pointer>
        requires is_raw_pointer_v<Pointer>
    Pointer add(Pointer p, difference_type n) const noexcept {
        return p + (n < (m_end - p) ? n : n - (m_end - m_start));
    }

    template<typename Pointer>
        requires is_raw_pointer_v<Pointer>
    Pointer sub(Pointer p, difference_type n) const noexcept {
        return p - (n > (p - m_start) ? n - (m_end - m_start) : n);
    }

    pointer map_pointer(pointer p) const noexcept {
        return p == 0 ? m_last : p;
    }

    void destory_content() noexcept {
        if constexpr (std::is_scalar_v<value_type>) {
            m_first = add(m_first, size());
        } else {
            for (size_type i = 0; i < m_size; ++i, increase(m_first)) {
                std::destroy_at(m_first);
            }
        }
    }

    void destroy() noexcept {
        destory_content();
        Alloc{}.deallocate(m_start, capacity());
    }

    void initialize_buffer(capacity_type buffer_capacity) {
        m_start = Alloc{}.allocate(buffer_capacity);
        m_end = m_start + buffer_capacity;
    }

    void
    initialize_buffer(capacity_type buffer_capacity, const value_type &item) {
        initialize_buffer(buffer_capacity);
        if constexpr (std::is_nothrow_copy_constructible_v<value_type>) {
            std::uninitialized_fill_n(m_first, buffer_capacity, item);
        } else {
            try {
                std::uninitialized_fill_n(m_first, buffer_capacity, item);
            } catch (...) {
                Alloc{}.deallocate(m_start, size());
                throw;
            }
        }
    }

    void reset(
        pointer start, pointer last, capacity_type new_capacity
    ) noexcept(noexcept(destroy())) {
        destroy();
        m_size = last - start;
        m_first = m_start = start;
        m_end = m_start + new_capacity;
        m_last = last == m_end ? m_start : last;
    }
};

} // namespace co_context
