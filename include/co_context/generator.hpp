////////////////////////////////////////////////////////////////
// Reference implementation of std::generator proposal P2502R2
// Authors: Casey Carter, Lewis Baker, Corentin Jabot.
// https://godbolt.org/z/5hcaPcfvP
//
#pragma once
#ifdef CO_CONTEXT_HAS_STD_GENERATOR
#include <generator>

namespace co_context {
using std::generator;
}
#elif !CO_CONTEXT_NO_GENERATOR
#pragma GCC system_header
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#define EMPTY_BASES __declspec(empty_bases)
#ifdef __clang__
#define NO_UNIQUE_ADDRESS
#else
#define NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#endif
#else
#define EMPTY_BASES
#define NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

// NOLINTBEGIN
namespace co_context {

struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) _Aligned_block {
    unsigned char _Pad[__STDCPP_DEFAULT_NEW_ALIGNMENT__];
};

template<class _Alloc>
using _Rebind = typename std::allocator_traits<_Alloc>::template rebind_alloc<
    _Aligned_block>;

template<class _Alloc>
concept _Has_real_pointers =
    std::same_as<_Alloc, void>
    || std::is_pointer_v<typename std::allocator_traits<_Alloc>::pointer>;

template<class _Allocator = void>
class _Promise_allocator { // statically specified allocator type
  private:
    using _Alloc = _Rebind<_Allocator>;

    static void *_Allocate(_Alloc _Al, const size_t _Size) {
        if constexpr (std::default_initializable<_Alloc> && std::allocator_traits<_Alloc>::is_always_equal::value) {
            // do not store stateless allocator
            const size_t _Count =
                (_Size + sizeof(_Aligned_block) - 1) / sizeof(_Aligned_block);
            return _Al.allocate(_Count);
        } else {
            // store stateful allocator
            static constexpr size_t _Align =
                (::std::max)(alignof(_Alloc), sizeof(_Aligned_block));
            const size_t _Count =
                (_Size + sizeof(_Alloc) + _Align - 1) / sizeof(_Aligned_block);
            void *const _Ptr = _Al.allocate(_Count);
            const auto _Al_address = (reinterpret_cast<uintptr_t>(_Ptr) + _Size
                                      + alignof(_Alloc) - 1)
                                     & ~(alignof(_Alloc) - 1);
            ::new (reinterpret_cast<void *>(_Al_address))
                _Alloc(::std::move(_Al));
            return _Ptr;
        }
    }

  public:
    static void *operator new(const size_t _Size)
        requires std::default_initializable<_Alloc>
    {
        return _Allocate(_Alloc{}, _Size);
    }

    template<class _Alloc2, class... _Args>
        requires std::convertible_to<const _Alloc2 &, _Allocator>
    static void *operator new(
        const size_t _Size,
        std::allocator_arg_t,
        const _Alloc2 &_Al,
        const _Args &...
    ) {
        return _Allocate(
            static_cast<_Alloc>(static_cast<_Allocator>(_Al)), _Size
        );
    }

    template<class _This, class _Alloc2, class... _Args>
        requires std::convertible_to<const _Alloc2 &, _Allocator>
    static void *operator new(
        const size_t _Size,
        const _This &,
        std::allocator_arg_t,
        const _Alloc2 &_Al,
        const _Args &...
    ) {
        return _Allocate(
            static_cast<_Alloc>(static_cast<_Allocator>(_Al)), _Size
        );
    }

    static void operator delete(void *const _Ptr, const size_t _Size) noexcept {
        if constexpr (std::default_initializable<_Alloc> && std::allocator_traits<_Alloc>::is_always_equal::value) {
            // make stateless allocator
            _Alloc _Al{};
            const size_t _Count =
                (_Size + sizeof(_Aligned_block) - 1) / sizeof(_Aligned_block);
            _Al.deallocate(static_cast<_Aligned_block *>(_Ptr), _Count);
        } else {
            // retrieve stateful allocator
            const auto _Al_address = (reinterpret_cast<uintptr_t>(_Ptr) + _Size
                                      + alignof(_Alloc) - 1)
                                     & ~(alignof(_Alloc) - 1);
            auto &_Stored_al = *reinterpret_cast<_Alloc *>(_Al_address);
            _Alloc _Al{::std::move(_Stored_al)};
            _Stored_al.~_Alloc();

            static constexpr size_t _Align =
                (::std::max)(alignof(_Alloc), sizeof(_Aligned_block));
            const size_t _Count =
                (_Size + sizeof(_Alloc) + _Align - 1) / sizeof(_Aligned_block);
            _Al.deallocate(static_cast<_Aligned_block *>(_Ptr), _Count);
        }
    }
};

template<>
class _Promise_allocator<void> { // type-erased allocator
  private:
    using _Dealloc_fn = void (*)(void *, size_t);

    template<class _ProtoAlloc>
    static void *_Allocate(const _ProtoAlloc &_Proto, size_t _Size) {
        using _Alloc = _Rebind<_ProtoAlloc>;
        auto _Al = static_cast<_Alloc>(_Proto);

        if constexpr (std::default_initializable<_Alloc> && std::allocator_traits<_Alloc>::is_always_equal::value) {
            // don't store stateless allocator
            const _Dealloc_fn _Dealloc = [](void *const _Ptr,
                                            const size_t _Size) {
                _Alloc _Al{};
                const size_t _Count =
                    (_Size + sizeof(_Dealloc_fn) + sizeof(_Aligned_block) - 1)
                    / sizeof(_Aligned_block);
                _Al.deallocate(static_cast<_Aligned_block *>(_Ptr), _Count);
            };

            const size_t _Count =
                (_Size + sizeof(_Dealloc_fn) + sizeof(_Aligned_block) - 1)
                / sizeof(_Aligned_block);
            void *const _Ptr = _Al.allocate(_Count);
            ::memcpy(
                static_cast<char *>(_Ptr) + _Size, &_Dealloc, sizeof(_Dealloc)
            );
            return _Ptr;
        } else {
            // store stateful allocator
            static constexpr size_t _Align =
                (::std::max)(alignof(_Alloc), sizeof(_Aligned_block));

            const _Dealloc_fn _Dealloc = [](void *const _Ptr, size_t _Size) {
                _Size += sizeof(_Dealloc_fn);
                const auto _Al_address = (reinterpret_cast<uintptr_t>(_Ptr)
                                          + _Size + alignof(_Alloc) - 1)
                                         & ~(alignof(_Alloc) - 1);
                auto &_Stored_al =
                    *reinterpret_cast<const _Alloc *>(_Al_address);
                _Alloc _Al{::std::move(_Stored_al)};
                _Stored_al.~_Alloc();

                const size_t _Count =
                    (_Size + sizeof(_Al) + _Align - 1) / sizeof(_Aligned_block);
                _Al.deallocate(static_cast<_Aligned_block *>(_Ptr), _Count);
            };

            const size_t _Count =
                (_Size + sizeof(_Dealloc_fn) + sizeof(_Al) + _Align - 1)
                / sizeof(_Aligned_block);
            void *const _Ptr = _Al.allocate(_Count);
            ::memcpy(
                static_cast<char *>(_Ptr) + _Size, &_Dealloc, sizeof(_Dealloc)
            );
            _Size += sizeof(_Dealloc_fn);
            const auto _Al_address = (reinterpret_cast<uintptr_t>(_Ptr) + _Size
                                      + alignof(_Alloc) - 1)
                                     & ~(alignof(_Alloc) - 1);
            ::new (reinterpret_cast<void *>(_Al_address))
                _Alloc{::std::move(_Al)};
            return _Ptr;
        }
    }

  public:
    static void *operator new(const size_t _Size) { // default: new/delete
        void *const _Ptr = ::operator new[](_Size + sizeof(_Dealloc_fn));
        const _Dealloc_fn _Dealloc = [](void *const _Ptr, const size_t _Size) {
            ::operator delete[](_Ptr, _Size + sizeof(_Dealloc_fn));
        };
        ::memcpy(
            static_cast<char *>(_Ptr) + _Size, &_Dealloc, sizeof(_Dealloc_fn)
        );
        return _Ptr;
    }

    template<class _Alloc, class... _Args>
    static void *operator new(
        const size_t _Size,
        std::allocator_arg_t,
        const _Alloc &_Al,
        const _Args &...
    ) {
        static_assert(
            _Has_real_pointers<_Alloc>,
            "coroutine allocators must use true pointers"
        );
        return _Allocate(_Al, _Size);
    }

    template<class _This, class _Alloc, class... _Args>
    static void *operator new(
        const size_t _Size,
        const _This &,
        std::allocator_arg_t,
        const _Alloc &_Al,
        const _Args &...
    ) {
        static_assert(
            _Has_real_pointers<_Alloc>,
            "coroutine allocators must use true pointers"
        );
        return _Allocate(_Al, _Size);
    }

    static void operator delete(void *const _Ptr, const size_t _Size) noexcept {
        _Dealloc_fn _Dealloc;
        ::memcpy(
            &_Dealloc, static_cast<const char *>(_Ptr) + _Size,
            sizeof(_Dealloc_fn)
        );
        _Dealloc(_Ptr, _Size);
    }
};

namespace ranges {
    template<std::ranges::range _Rng, class _Alloc = std::allocator<std::byte>>
    struct elements_of {
        NO_UNIQUE_ADDRESS _Rng range;
        NO_UNIQUE_ADDRESS _Alloc allocator{};
    };

    template<class _Rng, class _Alloc = std::allocator<std::byte>>
    elements_of(_Rng &&, _Alloc = {}) -> elements_of<_Rng &&, _Alloc>;
} // namespace ranges

template<class _Rty, class _Vty = void, class _Alloc = void>
class generator;

template<class _Rty, class _Vty>
using _Gen_value_t =
    std::conditional_t<std::is_void_v<_Vty>, std::remove_cvref_t<_Rty>, _Vty>;
template<class _Rty, class _Vty>
using _Gen_reference_t =
    std::conditional_t<std::is_void_v<_Vty>, _Rty &&, _Rty>;
template<class _Ref>
using _Gen_yield_t =
    std::conditional_t<std::is_reference_v<_Ref>, _Ref, const _Ref &>;

template<class _Yielded>
class _Gen_promise_base {
  public:
    static_assert(std::is_reference_v<_Yielded>);

    /* [[nodiscard]] */ std::suspend_always initial_suspend() noexcept {
        return {};
    }

    [[nodiscard]]
    auto final_suspend() noexcept {
        return _Final_awaiter{};
    }

    [[nodiscard]]
    std::suspend_always yield_value(_Yielded _Val) noexcept {
        _Ptr = ::std::addressof(_Val);
        return {};
    }

    // clang-format off
    [[nodiscard]]
    auto yield_value(const std::remove_reference_t<_Yielded>& _Val)
        noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<_Yielded>, const std::remove_reference_t<_Yielded>&>)
        requires (std::is_rvalue_reference_v<_Yielded> &&
            std::constructible_from<std::remove_cvref_t<_Yielded>, const std::remove_reference_t<_Yielded>&>) {
        return _Element_awaiter{_Val};
    }

    // clang-format on

    // clang-format off
    template <class _Rty, class _Vty, class _Alloc, class _Unused>
        requires std::same_as<_Gen_yield_t<_Gen_reference_t<_Rty, _Vty>>, _Yielded>
    [[nodiscard]]
    auto yield_value(
        ::co_context::ranges::elements_of<generator<_Rty, _Vty, _Alloc>&&, _Unused> _Elem) noexcept {
        return _Nested_awaitable<_Rty, _Vty, _Alloc>{std::move(_Elem.range)};
    }

    // clang-format on

    // clang-format off
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
    template <::std::ranges::input_range _Rng, class _Alloc>
        requires std::convertible_to<::std::ranges::range_reference_t<_Rng>, _Yielded>
    [[nodiscard]]
    auto yield_value(::co_context::ranges::elements_of<_Rng, _Alloc> _Elem) noexcept {
        // clang-format on
        using _Vty = ::std::ranges::range_value_t<_Rng>;
        return _Nested_awaitable<_Yielded, _Vty, _Alloc>{
            [](std::allocator_arg_t, _Alloc,
               ::std::ranges::iterator_t<_Rng> _It,
               const ::std::ranges::sentinel_t<_Rng> _Se
            ) -> generator<_Yielded, _Vty, _Alloc> {
                for (; _It != _Se; ++_It) {
                    co_yield static_cast<_Yielded>(*_It);
                }
            }(std::allocator_arg, _Elem.allocator,
              ::std::ranges::begin(_Elem.range),
              ::std::ranges::end(_Elem.range))
        };
    }

#pragma GCC diagnostic pop

    void await_transform() = delete;

    void return_void() noexcept {}

    void unhandled_exception() {
        if (_Info) {
            _Info->_Except = ::std::current_exception();
        } else {
            throw;
        }
    }

  private:
    struct _Element_awaiter {
        std::remove_cvref_t<_Yielded> _Val;

        [[nodiscard]]
        constexpr bool await_ready() const noexcept {
            return false;
        }

        template<class _Promise>
        constexpr void await_suspend(std::coroutine_handle<_Promise> _Handle
        ) noexcept {
#ifdef __cpp_lib_is_pointer_interconvertible
            static_assert(std::is_pointer_interconvertible_base_of_v<
                          _Gen_promise_base, _Promise>);
#endif // __cpp_lib_is_pointer_interconvertible

            _Gen_promise_base &_Current = _Handle.promise();
            _Current._Ptr = ::std::addressof(_Val);
        }

        constexpr void await_resume() const noexcept {}
    };

    struct _Nest_info {
        std::exception_ptr _Except;
        std::coroutine_handle<_Gen_promise_base> _Parent;
        std::coroutine_handle<_Gen_promise_base> _Root;
    };

    struct _Final_awaiter {
        [[nodiscard]]
        bool await_ready() noexcept {
            return false;
        }

        template<class _Promise>
        [[nodiscard]]
        std::coroutine_handle<>
        await_suspend(std::coroutine_handle<_Promise> _Handle) noexcept {
#ifdef __cpp_lib_is_pointer_interconvertible
            static_assert(std::is_pointer_interconvertible_base_of_v<
                          _Gen_promise_base, _Promise>);
#endif // __cpp_lib_is_pointer_interconvertible

            _Gen_promise_base &_Current = _Handle.promise();
            if (!_Current._Info) {
                return ::std::noop_coroutine();
            }

            std::coroutine_handle<_Gen_promise_base> _Cont =
                _Current._Info->_Parent;
            _Current._Info->_Root.promise()._Top = _Cont;
            _Current._Info = nullptr;
            return _Cont;
        }

        void await_resume() noexcept {}
    };

    template<class _Rty, class _Vty, class _Alloc>
    struct _Nested_awaitable {
        static_assert(std::same_as<
                      _Gen_yield_t<_Gen_reference_t<_Rty, _Vty>>,
                      _Yielded>);

        _Nest_info _Nested;
        generator<_Rty, _Vty, _Alloc> _Gen;

        explicit _Nested_awaitable(generator<_Rty, _Vty, _Alloc> &&_Gen_
        ) noexcept
            : _Gen(::std::move(_Gen_)) {}

        [[nodiscard]]
        bool await_ready() noexcept {
            return !_Gen._Coro;
        }

        template<class _Promise>
        [[nodiscard]]
        std::coroutine_handle<_Gen_promise_base>
        await_suspend(std::coroutine_handle<_Promise> _Current) noexcept {
#ifdef __cpp_lib_is_pointer_interconvertible
            static_assert(std::is_pointer_interconvertible_base_of_v<
                          _Gen_promise_base, _Promise>);
#endif // __cpp_lib_is_pointer_interconvertible
            auto _Target =
                std::coroutine_handle<_Gen_promise_base>::from_address(
                    _Gen._Coro.address()
                );
            _Nested._Parent =
                std::coroutine_handle<_Gen_promise_base>::from_address(
                    _Current.address()
                );
            _Gen_promise_base &_Parent_promise = _Nested._Parent.promise();
            if (_Parent_promise._Info) {
                _Nested._Root = _Parent_promise._Info->_Root;
            } else {
                _Nested._Root = _Nested._Parent;
            }
            _Nested._Root.promise()._Top = _Target;
            _Target.promise()._Info = ::std::addressof(_Nested);
            return _Target;
        }

        void await_resume() {
            if (_Nested._Except) [[unlikely]] {
                ::std::rethrow_exception(::std::move(_Nested._Except));
            }
        }
    };

    template<class, class>
    friend class _Gen_iter;

    // _Top and _Info are mutually exclusive, and could potentially be merged.
    std::coroutine_handle<_Gen_promise_base> _Top =
        std::coroutine_handle<_Gen_promise_base>::from_promise(*this);
    std::add_pointer_t<_Yielded> _Ptr = nullptr;
    _Nest_info *_Info = nullptr;
};

struct _Gen_secret_tag {};

template<class _Value, class _Ref>
class _Gen_iter {
  public:
    using value_type = _Value;
    using difference_type = ptrdiff_t;

    _Gen_iter(_Gen_iter &&_That) noexcept
        : _Coro{::std::exchange(_That._Coro, {})} {}

    _Gen_iter &operator=(_Gen_iter &&_That) noexcept {
        _Coro = ::std::exchange(_That._Coro, {});
        return *this;
    }

    [[nodiscard]]
    _Ref
    operator*() const noexcept {
        assert(!_Coro.done() && "Can't dereference generator end iterator");
        return static_cast<_Ref>(*_Coro.promise()._Top.promise()._Ptr);
    }

    _Gen_iter &operator++() {
        assert(!_Coro.done() && "Can't increment generator end iterator");
        _Coro.promise()._Top.resume();
        return *this;
    }

    void operator++(int) { ++*this; }

    [[nodiscard]]
    bool
    operator==(std::default_sentinel_t) const noexcept {
        return _Coro.done();
    }

  private:
    template<class, class, class>
    friend class generator;

    explicit _Gen_iter(
        _Gen_secret_tag,
        std::coroutine_handle<_Gen_promise_base<_Gen_yield_t<_Ref>>> _Coro_
    ) noexcept
        : _Coro{_Coro_} {}

    std::coroutine_handle<_Gen_promise_base<_Gen_yield_t<_Ref>>> _Coro;
};

template<class _Rty, class _Vty, class _Alloc>
class generator
    : public std::ranges::view_interface<generator<_Rty, _Vty, _Alloc>> {
  private:
    using _Value = _Gen_value_t<_Rty, _Vty>;
    static_assert(
        std::same_as<std::remove_cvref_t<_Value>, _Value>
            && std::is_object_v<_Value>,
        "generator's value type must be a cv-unqualified object type"
    );

    // clang-format off
    using _Ref = _Gen_reference_t<_Rty, _Vty>;
    static_assert(std::is_reference_v<_Ref>
        || (std::is_object_v<_Ref> && std::same_as<std::remove_cv_t<_Ref>, _Ref> && std::copy_constructible<_Ref>),
        "generator's second argument must be a reference type or a cv-unqualified "
        "copy-constructible object type");

    using _RRef = std::conditional_t<std::is_lvalue_reference_v<_Ref>, std::remove_reference_t<_Ref>&&, _Ref>;

    static_assert(std::common_reference_with<_Ref&&, _Value&> && std::common_reference_with<_Ref&&, _RRef&&>
        && std::common_reference_with<_RRef&&, const _Value&>,
        "an iterator with the selected value and reference types cannot model indirectly_readable");
    // clang-format on

    static_assert(
        _Has_real_pointers<_Alloc>,
        "generator allocators must use true pointers"
    );

    friend _Gen_promise_base<_Gen_yield_t<_Ref>>;

  public:
    struct EMPTY_BASES promise_type
        : _Promise_allocator<_Alloc>
        , _Gen_promise_base<_Gen_yield_t<_Ref>> {
        [[nodiscard]]
        generator get_return_object() noexcept {
            return generator{
                _Gen_secret_tag{},
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }
    };

    static_assert(std::is_standard_layout_v<promise_type>);
#ifdef __cpp_lib_is_pointer_interconvertible
    static_assert(std::is_pointer_interconvertible_base_of_v<
                  _Gen_promise_base<_Gen_yield_t<_Ref>>,
                  promise_type>);
#endif // __cpp_lib_is_pointer_interconvertible

    generator(generator &&_That) noexcept
        : _Coro(::std::exchange(_That._Coro, {})) {}

    ~generator() {
        if (_Coro) {
            _Coro.destroy();
        }
    }

    generator &operator=(generator _That) noexcept {
        ::std::swap(_Coro, _That._Coro);
        return *this;
    }

    [[nodiscard]]
    _Gen_iter<_Value, _Ref> begin() {
        // Pre: _Coro is suspended at its initial suspend point
        assert(_Coro && "Can't call begin on moved-from generator");
        _Coro.resume();
        return _Gen_iter<_Value, _Ref>{
            _Gen_secret_tag{},
            std::coroutine_handle<_Gen_promise_base<_Gen_yield_t<_Ref>>>::
                from_address(_Coro.address())
        };
    }

    [[nodiscard]]
    std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

  private:
    std::coroutine_handle<promise_type> _Coro = nullptr;

    explicit generator(
        _Gen_secret_tag, std::coroutine_handle<promise_type> _Coro_
    ) noexcept
        : _Coro(_Coro_) {}
};
} // namespace co_context

// NOLINTEND
#endif
