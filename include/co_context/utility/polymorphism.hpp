#pragma once

namespace co_context {

// Overload pattern
template<typename... F>
struct overload : F... {
    using F::operator()...;
};

template<typename... F>
overload(F...) -> overload<F...>;

// CRTP
template<typename T, template<typename> class Interface>
struct CRTP /*NOLINT*/ {
    T &self() { return static_cast<T &>(*this); }

    const T &self() const { return static_cast<const T &>(*this); }

  private:
    CRTP() = default;
    friend Interface<T>;
};

} // namespace co_context
