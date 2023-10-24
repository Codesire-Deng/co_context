#include <co_context/mpl/type_list.hpp>

using namespace co_context;

template<typename... Ts>
struct s {};

using list = mpl::type_list<char, int, float, double>;
using empty = mpl::type_list<>;

void head_tails_last() {
    static_assert(std::is_same_v<mpl::head_t<list>, char>);
    static_assert(std::is_same_v<
                  mpl::tails_t<list>, mpl::type_list<int, float, double>>);
    static_assert(std::is_same_v<mpl::last_t<list>, double>);
}

void drop_last() {
    using no_last = mpl::type_list<char, int, float>;
    static_assert(std::is_same_v<mpl::drop_last_t<list>, no_last>);
}

void from_to() {
    using s_list = s<char, int, float, double>;
    static_assert(std::is_same_v<mpl::type_list_from_t<s_list>, list>);
    static_assert(std::is_same_v<list::template to<s>, s_list>);
}

template<typename T>
struct s_pair {
    using type = s<T, T>;
};

void map() {
    using pair_list = mpl::type_list<
        s<char, char>, s<int, int>, s<float, float>, s<double, double>>;
    static_assert(std::is_same_v<mpl::map_t<list, s_pair>, pair_list>);
}

void filter() {
    using float_list = mpl::type_list<float, double>;
    static_assert(std::is_same_v<
                  mpl::filter_t<list, std::is_floating_point>, float_list>);
}

void remove() {
    using no_int_list = mpl::type_list<char, float, double>;
    static_assert(std::is_same_v<mpl::remove_t<list, int>, no_int_list>);
}

void remove_if() {
    using no_floating_list = mpl::type_list<char, int>;
    static_assert(std::is_same_v<
                  mpl::remove_if_t<list, std::is_floating_point>,
                  no_floating_list>);
}

template<typename T, typename U>
struct max_sizeof
    : std::integral_constant<
          size_t,
          (sizeof(T) > sizeof(U) ? sizeof(T) : sizeof(U))> {};

void fold() {
    using zero = std::integral_constant<size_t, 0>;
    static_assert(mpl::fold_t<list, zero, max_sizeof>::value == sizeof(double));
}

void concat() {
    static_assert(std::is_same_v<mpl::concat_t<empty>, empty>);
    static_assert(std::is_same_v<mpl::concat_t<list, empty>, list>);
    static_assert(std::is_same_v<mpl::concat_t<empty, list>, list>);

    using double_list =
        mpl::type_list<char, int, float, double, char, int, float, double>;
    static_assert(std::is_same_v<mpl::concat_t<list, list>, double_list>);
}

void contain() {
    static_assert(mpl::contain_v<list, float>);
    static_assert(!mpl::contain_v<list, void>);
}

void find() {
    static_assert(mpl::find_v<list, char> == 0);
    static_assert(mpl::find_v<list, int> == 1);
    static_assert(mpl::find_v<list, float> == 2);
    static_assert(mpl::find_v<list, double> == 3);
    static_assert(mpl::find_v<list, void> == mpl::npos);

    static_assert(mpl::find_if_v<list, std::is_floating_point> == 2);
    static_assert(mpl::find_if_v<list, std::is_void> == mpl::npos);
}

void reverse() {
    using r_list = mpl::type_list<double, float, int, char>;
    static_assert(std::is_same_v<mpl::reverse_t<list>, r_list>);
}

void count() {
    static_assert(mpl::count_v<list, float> == 1);
    static_assert(mpl::count_v<list, void> == 0);

    using double_list =
        mpl::type_list<char, int, float, double, char, int, float, double>;
    static_assert(mpl::count_v<double_list, float> == 2);
    static_assert(mpl::count_v<double_list, void> == 0);
}

void at() {
    static_assert(std::is_same_v<mpl::at_t<list, 0>, char>);
    static_assert(std::is_same_v<mpl::at_t<list, 1>, int>);
    static_assert(std::is_same_v<mpl::at_t<list, 2>, float>);
    static_assert(std::is_same_v<mpl::at_t<list, 3>, double>);
}

void first_n() {
    static_assert(std::is_same_v<mpl::first_n_t<list, 0>, empty>);
    static_assert(std::is_same_v<
                  mpl::first_n_t<list, 1>, mpl::type_list<char>>);
    static_assert(std::is_same_v<
                  mpl::first_n_t<list, 2>, mpl::type_list<char, int>>);
    static_assert(std::is_same_v<
                  mpl::first_n_t<list, 3>, mpl::type_list<char, int, float>>);
    static_assert(std::is_same_v<mpl::first_n_t<list, 4>, list>);
}

void replace() {
    using int_to_void_list = mpl::type_list<char, void, float, double>;
    static_assert(std::is_same_v<
                  mpl::replace_t<list, int, void>, int_to_void_list>);
}

void set() {
    using a = mpl::type_list<void, char, int>;
    using b = mpl::type_list<char, int, float>;

    static_assert(std::is_same_v<
                  mpl::union_set_t<a, b>,
                  mpl::type_list<void, char, int, float>>);

    static_assert(std::is_same_v<
                  mpl::intersection_set_t<a, b>, mpl::type_list<char, int>>);

    static_assert(std::is_same_v<
                  mpl::difference_set_t<a, b>, mpl::type_list<void>>);

    static_assert(std::is_same_v<
                  mpl::difference_set_t<a, b>, mpl::type_list<void>>);

    static_assert(std::is_same_v<
                  mpl::symmetric_difference_set_t<a, b>,
                  mpl::type_list<void, float>>);
}

void split() {
    using sp_list =
        mpl::type_list<mpl::type_list<char, int>, mpl::type_list<double>>;
    static_assert(std::is_same_v<mpl::split_t<list, float>, sp_list>);

    using no_floating_list = mpl::type_list<mpl::type_list<char, int>>;
    static_assert(std::is_same_v<
                  mpl::split_if_t<list, std::is_floating_point>,
                  no_floating_list>);
}

int main() {
}
