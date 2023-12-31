#if !CO_CONTEXT_NO_GENERATOR
#include <co_context/generator.hpp>

/////////////////////////////////////////////////////////////////////////////
// Examples start here

#include <array>
#include <cinttypes>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>

#pragma GCC diagnostic ignored "-Wmismatched-new-delete"

namespace {
///////////////////////
// Simple non-nested serial generator

co_context::generator<uint64_t> fib(int max) {
    auto a = 0, b = 1;
    for (auto n = 0; n < max; n++) {
        co_yield std::exchange(a, std::exchange(b, a + b));
    }
} // namespace co_context::generator<uint64_t>fib(intmax)

co_context::generator<int> other_generator(int i, int j) {
    while (i != j) {
        co_yield i++;
    }
}

/////////////////////
// Demonstrate ability to yield nested sequences
//
// Need to use co_context::ranges::elements_of() to trigger yielding elements of
// nested sequence.
//
// Supports yielding same generator type (with efficient resumption for
// recursive cases)
//
// Also supports yielding any other range whose elements are convertible to
// the current generator's elements.

co_context::generator<uint64_t, uint64_t> nested_sequences_example() {
    std::printf("yielding elements_of std::array\n");
#if defined(__GNUC__) && !defined(__clang__)
    co_yield co_context::ranges::elements_of(
        std::array<const uint64_t, 5>{2, 4, 6, 8, 10}, {}
    );
#else
    co_yield co_context::ranges::elements_of{
        std::array<const uint64_t, 5>{2, 4, 6, 8, 10}
    };
#endif
    std::printf("yielding elements_of nested co_context::generator\n");
    co_yield co_context::ranges::elements_of{fib(10)};

    std::printf("yielding elements_of other kind of generator\n");
    co_yield co_context::ranges::elements_of{other_generator(5, 8)};
}

//////////////////////////////////////
// Following examples show difference between:
//
//                                    If I co_yield a...
//                              X / X&&  | X&         | const X&
//                           ------------+------------+-----------
// - generator<X, X>               (same as generator<X, X&&>)
// - generator<X, const X&>   ref        | ref        | ref
// - generator<X, X&&>        ref        | ill-formed | ill-formed
// - generator<X, X&>         ill-formed | ref        | ill-formed

struct X {
    int id;

    X(int id) : id(id) { std::printf("X::X(%i)\n", id); }

    X(const X &x) : id(x.id) { std::printf("X::X(copy %i)\n", id); }

    X(X &&x) : id(std::exchange(x.id, -1)) {
        std::printf("X::X(move %i)\n", id);
    }

    ~X() { std::printf("X::~X(%i)\n", id); }
};

co_context::generator<X> always_ref_example() {
    co_yield X{1};
    {
        X x{2};
        co_yield x;
        assert(x.id == 2);
    }
    {
        const X x{3};
        co_yield x;
        assert(x.id == 3);
    }
    {
        X x{4};
        co_yield std::move(x);
    }
}

co_context::generator<X &&> xvalue_example() {
    co_yield X{1};
    X x{2};
    co_yield x; // well-formed: generated element is copy of lvalue
    assert(x.id == 2);
    co_yield std::move(x);
}

co_context::generator<const X &> const_lvalue_example() {
    co_yield X{1}; // OK
    const X x{2};
    co_yield x;            // OK
    co_yield std::move(x); // OK: same as above
}

co_context::generator<X &> lvalue_example() {
    // co_yield X{1}; // ill-formed: prvalue -> non-const lvalue
    X x{2};
    co_yield x; // OK
    // co_yield std::move(x); // ill-formed: xvalue -> non-const lvalue
}

///////////////////////////////////
// These examples show different usages of reference/value_type
// template parameters

// value_type = std::unique_ptr<int>
// reference = std::unique_ptr<int>&&
co_context::generator<std::unique_ptr<int> &&> unique_ints(const int high) {
    for (auto i = 0; i < high; ++i) {
        co_yield std::make_unique<int>(i);
    }
}

// value_type = std::string_view
// reference = std::string_view&&
co_context::generator<std::string_view> string_views() {
    co_yield "foo";
    co_yield "bar";
}

// value_type = std::string
// reference = std::string_view
template<typename Allocator>
co_context::generator<std::string_view, std::string>
strings(std::allocator_arg_t, Allocator) {
    co_yield {};
    co_yield "start";
    for (auto sv : string_views()) {
        co_yield std::string{sv} + '!';
    }
    co_yield "end";
}

// Resulting vector is deduced using ::value_type.
template<std::ranges::input_range R>
std::vector<std::ranges::range_value_t<R>> to_vector(R &&r) {
    std::vector<std::ranges::range_value_t<R>> v;
    for (auto &&x : r) {
        v.emplace_back(static_cast<decltype(x) &&>(x));
    }
    return v;
}

// zip() algorithm produces a generator of tuples where
// the reference type is a tuple of references and
// the value type is a tuple of values.
template<std::ranges::range... Rs, std::size_t... Indices>
co_context::generator<
    std::tuple<std::ranges::range_reference_t<Rs>...>,
    std::tuple<std::ranges::range_value_t<Rs>...>>
zip_impl(std::index_sequence<Indices...>, Rs... rs) {
    std::tuple<std::ranges::iterator_t<Rs>...> its{std::ranges::begin(rs)...};
    std::tuple<std::ranges::sentinel_t<Rs>...> itEnds{std::ranges::end(rs)...};
    while (((std::get<Indices>(its) != std::get<Indices>(itEnds)) && ...)) {
        co_yield {*std::get<Indices>(its)...};
        (++std::get<Indices>(its), ...);
    }
}

template<std::ranges::range... Rs>
co_context::generator<
    std::tuple<std::ranges::range_reference_t<Rs>...>,
    std::tuple<std::ranges::range_value_t<Rs>...>>
zip(Rs &&...rs) {
    return zip_impl(
        std::index_sequence_for<Rs...>{},
        std::views::all(std::forward<Rs>(rs))...
    );
}

void value_type_example() {
    std::vector<std::string_view> s1 = to_vector(string_views());
    for (auto &s : s1) {
        std::printf("\"%*s\"\n", (int)s.size(), s.data());
    }

    std::printf("\n");

    std::vector<std::string> s2 =
        to_vector(strings(std::allocator_arg, std::allocator<std::byte>{}));
    for (auto &s : s2) {
        std::printf("\"%s\"\n", s.c_str());
    }

    std::printf("\n");

    std::vector<std::tuple<std::string, std::string>> s3 = to_vector(
        zip(strings(std::allocator_arg, std::allocator<std::byte>{}),
            strings(std::allocator_arg, std::allocator<std::byte>{}))
    );
    for (auto &[a, b] : s3) {
        std::printf("\"%s\", \"%s\"\n", a.c_str(), b.c_str());
    }
}

template<typename T>
struct stateful_allocator {
    using value_type = T;

    int id;

    explicit stateful_allocator(int id) noexcept : id(id) {}

    template<typename U>
    stateful_allocator(const stateful_allocator<U> &x) : id(x.id) {}

    T *allocate(std::size_t count) {
        std::printf("stateful_allocator(%i).allocate(%zu)\n", id, count);
        return std::allocator<T>().allocate(count);
    }

    void deallocate(T *ptr, std::size_t count) noexcept {
        std::printf("stateful_allocator(%i).deallocate(%zu)\n", id, count);
        std::allocator<T>().deallocate(ptr, count);
    }

    template<typename U>
    bool operator==(const stateful_allocator<U> &x) const {
        return this->id == x.id;
    }
};

co_context::generator<int, void, std::allocator<std::byte>>
stateless_example() {
    co_yield 42;
}

co_context::generator<int, void, std::allocator<std::byte>>
stateless_example(std::allocator_arg_t, std::allocator<std::byte>) {
    co_yield 42;
}

template<typename Allocator>
co_context::generator<int, void, Allocator>
stateful_alloc_example(std::allocator_arg_t, Allocator) {
    co_yield 42;
}

struct member_coro {
    co_context::generator<int> f() const { co_yield 42; }
};
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

    std::printf("nested_sequences_example\n");
    for (uint64_t a : nested_sequences_example()) {
        std::printf("-> %" PRIu64 "\n", a);
    }

    std::printf("\nby_value_example\n");

    for (auto &&x : always_ref_example()) {
        std::printf("-> %i\n", x.id);
    }

    std::printf("\nby_rvalue_ref_example\n");

    for (auto &&x : xvalue_example()) {
        std::printf("-> %i\n", x.id);
    }

    std::printf("\nby_const_ref_example\n");

    for (auto &&x : const_lvalue_example()) {
        std::printf("-> %i\n", x.id);
    }

    std::printf("\nby_lvalue_ref_example\n");

    for (auto &&x : lvalue_example()) {
        std::printf("-> %i\n", x.id);
    }

    std::printf("\nvalue_type example\n");

    value_type_example();

    std::printf("\nmove_only example\n");

    for (std::unique_ptr<int> ptr : unique_ints(5)) {
        std::printf("-> %i\n", *ptr);
    }

    std::printf("\nstateless_alloc examples\n");

    stateless_example();
    stateless_example(std::allocator_arg, std::allocator<float>{});

    std::printf("\nstateful_alloc example\n");

    stateful_alloc_example(std::allocator_arg, stateful_allocator<double>{42});

    [[maybe_unused]] member_coro m;
    assert(*m.f().begin() == 42);
}

#else // if !CO_CONTEXT_NO_GENERATOR

#include <iostream>

int main() {
    std::cout
        << "This program requires g++ 11.3 or clang 17 as the compiler. exit..."
        << std::endl;
    return 0;
}

#endif
