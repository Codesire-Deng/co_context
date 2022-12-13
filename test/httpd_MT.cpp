// Multi-threaded http GET server
#include "co_context/all.hpp"
#include <algorithm>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

using namespace co_context;
using namespace std::string_view_literals;

static_assert(liburingcxx::is_kernel_reach(5, 5));

constexpr std::string_view SERVER_STRING = "Server: zerohttpd/1.0\r\n";
constexpr uint16_t DEFAULT_SERVER_PORT = 8000;
constexpr size_t READ_SZ = 4096;
constexpr uint32_t worker_num = 4;
io_context ctx[worker_num];

constexpr std::string_view unimplemented_content =
    "HTTP/1.0 400 Bad Request\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<title>ZeroHTTPd: Unimplemented</title>"
    "</head>"
    "<body>"
    "<h1>Bad Request (Unimplemented)</h1>"
    "<p>Your client sent a request ZeroHTTPd did not understand and it is probably not your fault.</p>"
    "</body>"
    "</html>"sv;

constexpr std::string_view http_404_content =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-type: text/html\r\n"
    "\r\n"
    "<html>"
    "<head>"
    "<title>ZeroHTTPd: Not Found</title>"
    "</head>"
    "<body>"
    "<h1>Not Found (404)</h1>"
    "<p>Your client is asking for an object that was not found on this server.</p>"
    "</body>"
    "</html>"sv;

/*
 One function that prints the system call and the error details
 and then exits with error code 1. Non-zero meaning things didn't go well.
 */
void fatal_error(const char *syscall) {
    perror(syscall);
    std::terminate();
}

void check_for_index_file() {
    struct stat st;
    int ret = stat("public/index.html", &st);
    if (ret < 0) {
        log::e("ZeroHTTPd needs the \"public\" directory to be "
               "present in the current directory.\n");
        fatal_error("stat: public/index.html");
    }
}

/*
 * Once a static file is identified to be served, this function is used to read
 * the file and write it over the client socket using Linux's sendfile() system
 * call. This saves us the hassle of transferring file buffers from kernel to
 * user space and back.
 * */
task<void> prep_file_contents(
    std::string_view file_path, off_t file_size, std::span<char> content_buffer
) {
    int fd;

    fd = ::open(file_path.data(), O_RDONLY);
    if (fd < 0) {
        fatal_error("read");
    }

    /* We should really check for short reads here */
    int ret = co_await lazy::read(fd, content_buffer, 0);

    if (ret < file_size) [[unlikely]] {
        log::w("Encountered a short read.\n");
    }

    ::close(fd);
}

/*
 * Simple function to get the file extension of the file that we are about to
 * serve.
 * */

std::string_view get_filename_ext(std::string_view filename) {
    size_t pos = filename.rfind('.');
    if (pos == std::string_view::npos) {
        return ""sv;
    }
    return filename.substr(pos + 1);
}

void memwrite(std::span<char> &buf, std::string_view content) {
    assert(buf.size() >= content.size());
    std::memcpy(buf.data(), content.data(), content.size());
    buf = {buf.begin() + ssize_t(content.size()), buf.end()};
}

/*
 * Sends the HTTP 200 OK header, the server string, for a few types of files, it
 * can also send the content type based on the file extension. It also sends the
 * content length header. Finally it send a '\r\n' in a line by itself
 * signalling the end of headers and the beginning of any content.
 * */
size_t
prep_headers(std::string_view path, off_t len, std::span<char> send_buffer) {
    char format_buffer[64];
    const auto start = send_buffer.begin();

    memwrite(send_buffer, "HTTP/1.0 200 OK\r\n");
    memwrite(send_buffer, SERVER_STRING);

    /*
     * Check the file extension for certain common types of files
     * on web pages and send the appropriate content-type header.
     * Since extensions can be mixed case like JPG, jpg or Jpg,
     * we turn the extension into lower case before checking.
     * */
    std::string_view file_ext = get_filename_ext(path);
    if ("htm"sv == file_ext || "html"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: text/html\r\n");
    } else if ("png"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: image/png\r\n");
    } else if ("gif"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: image/gif\r\n");
    } else if ("jpg"sv == file_ext || "jpeg"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: image/jpeg\r\n");
    } else if ("js"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: application/javascript\r\n");
    } else if ("css"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: text/css\r\n");
    } else if ("txt"sv == file_ext) {
        memwrite(send_buffer, "Content-Type: text/plain\r\n");
    }

    (void)sprintf(format_buffer, "content-length: %ld\r\n", len);
    // BUG check
    memwrite(send_buffer, format_buffer);
    memwrite(send_buffer, "\r\n");

    return send_buffer.begin() - start;
}

std::span<char> get_line(std::span<char> src) {
    auto pos = std::string_view{src.begin(), src.end()}.find("\r\n");
    if (pos != std::string_view::npos) [[likely]] {
        return {src.subspan(0, pos)};
    } else {
        return {};
    }
}

std::span<char> token_split(std::span<char> &str, char c) {
    auto it = std::ranges::find(str, c);
    if (it != str.end()) [[likely]] {
        *it = '\0';
        std::span<char> result = {str.begin(), it + 1};
        str = {it + 1, str.end()};
        return result;
    } else {
        std::span<char> result = str;
        str = {};
        return result;
    }
}

void str_tolower(std::span<char> str) {
    for (char &c : str) {
        c = static_cast<char>(std::tolower(c));
    }
}

task<> session(const int sockfd) {
    co_context::socket sock{sockfd};
    defer _{[sockfd] {
        ::close(sockfd);
    }};

    char recv_buf[READ_SZ] = "public";
    std::memset(recv_buf + 6, 0, sizeof(recv_buf) - 6);
    int nr = co_await sock.recv({recv_buf + 6, sizeof(recv_buf) - 6});
    if (nr <= 0) [[unlikely]] {
        if (nr < 0) {
            log::e("Bad recv\n");
        }
        co_return;
    }

    std::span<char> head = get_line({recv_buf + 6, (size_t)nr});
    if (head.empty()) [[unlikely]] {
        log::e("Malformed request\n");
        co_return;
    }

    auto method = token_split(head, ' ');
    str_tolower(method);

    // only response `get`
    if (!std::ranges::equal(method, "get")) {
        // TODO
        co_await sock.send(unimplemented_content);
        co_return;
    }

    // get the filepath
    auto path = token_split(head, ' ');
    std::memcpy(path.data() - 6, "public", 6);
    if (*(path.rbegin() + 1) == '/') {
        std::strcpy(path.end().base() - 1, "index.html");
        path = {path.begin() - 6, path.end() + 10};
    } else {
        path = {path.begin() - 6, path.end()};
    }

    /* The stat() system call will give you information about the file
     * like type (regular file, directory, etc), size, etc.
     * Check if this is a normal/regular file and not a directory or
     * something else */
    struct stat path_stat;
    if (stat(path.data(), &path_stat) == -1 || !S_ISREG(path_stat.st_mode)) {
        log::i("404 Not Found: %s\n", path.data());
        co_await sock.send(http_404_content);
        co_return;
    }

    // get header prepared
    char header_buf[READ_SZ];
    std::string_view path_sv = {path.begin(), path.end()};
    const size_t n_header =
        prep_headers(path_sv, path_stat.st_size, header_buf);

    // get file_content prepared
    std::vector<char> content_buf{};
    content_buf.resize(path_stat.st_size);
    co_await prep_file_contents(path_sv, path_stat.st_size, content_buf);

    log::i("200 %s %ld bytes\n", path.data(), path_stat.st_size);

    // send them to the client
    co_await (sock.send({header_buf, n_header}) && sock.send(content_buf));
}

task<> server(acceptor &ac, int id) {
    log::i("ZeroHTTPd listening on port: %d\n", DEFAULT_SERVER_PORT);

    for (int sockfd; (sockfd = co_await ac.accept()) >= 0;) {
        co_spawn(session(sockfd));
    }
}

void sigint_handler([[maybe_unused]] int signo) {
    log::w("^C pressed. Shutting down.\n");
    std::terminate();
}

int main() {
    check_for_index_file();

    acceptor ac{inet_address{DEFAULT_SERVER_PORT}};

    for (int i = 0; i < int(worker_num); ++i) {
        ctx[i].co_spawn(server(ac, i));
    }

    for (auto &c : ctx) {
        c.start();
    }
    ctx[0].join(); // never stop

    return 0;
}