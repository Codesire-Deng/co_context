/*
 *  A type tester of liburingcxx.
 *
 *  Copyright (C) 2022 Zifeng Deng
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published
 *  by the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#if !__has_include(<liburing.h>)
#include <iostream>

int main() {
    std::cerr << "This test requires <liburing.h>.\n";
    return 1;
}
#else
#include <uring/uring.hpp>

#include <liburing.h>

#include <iostream>
#include <type_traits>

int main(int argc, char *argv[]) {
    using std::cout, std::endl;

    using namespace liburingcxx;
    using namespace liburingcxx::detail;
    using namespace std;
    static_assert(is_standard_layout_v<uring<0>> && !is_trivial_v<uring<0>>);
    static_assert(is_standard_layout_v<sq_entry> && is_trivial_v<sq_entry>);
    static_assert(is_standard_layout_v<cq_entry> && is_trivial_v<cq_entry>);
    static_assert(is_standard_layout_v<submission_queue> && is_trivial_v<submission_queue>);
    static_assert(is_standard_layout_v<completion_queue> && is_trivial_v<completion_queue>);

    static_assert(sizeof(io_uring_sqe) == sizeof(sq_entry));
    static_assert(sizeof(io_uring_cqe) == sizeof(cq_entry));
    static_assert(sizeof(io_uring_sq) != sizeof(submission_queue));
    static_assert(sizeof(io_uring_cq) != sizeof(completion_queue));

    cout << "All test passed!\n";

    return 0;
}
#endif
