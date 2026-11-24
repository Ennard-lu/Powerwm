// SPDX-License-Identifier: Apache-2.0

#include <platform/wayland/socket.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <utility>

namespace pwm::platform::wayland {

namespace {

constexpr usize max_fds = 64;

auto io_error(std::string_view message) noexcept -> error {
    return make_error(error_code::io, message, std::source_location::current(), errno);
}

auto make_address(std::string_view path, sockaddr_un& address) -> bool {
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    address = {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.data(), path.size());
    address.sun_path[path.size()] = '\0';
    return true;
}

auto set_nonblocking(handle_t fd) -> bool {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}

stream::stream(stream&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

stream::~stream() noexcept {
    destroy();
}

auto stream::operator=(stream&& other) noexcept -> stream& {
    if (this != &other) {
        destroy();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

auto stream::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto stream::fd() const noexcept -> handle_t {
    return fd_;
}

auto stream::receive(std::span<std::byte> data, std::span<handle_t> fds) noexcept
    -> result<receive_result> {
    std::array<char, CMSG_SPACE(sizeof(handle_t) * max_fds)> control{};
    iovec vector{.iov_base = data.data(), .iov_len = data.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    isize received = -1;
    do {
        received = ::recvmsg(fd_, &message, MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return receive_result{.would_block = true};
        }
        return std::unexpected(io_error("recvmsg failed"));
    }
    if (received == 0) {
        return receive_result{.closed = true};
    }

    usize fd_count = 0;
    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr;
         header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) {
            continue;
        }
        const usize payload = header->cmsg_len - CMSG_LEN(0);
        const usize count = payload / sizeof(handle_t);
        const auto* received_fds = reinterpret_cast<const handle_t*>(CMSG_DATA(header));
        for (usize index = 0; index < count; ++index) {
            if (fd_count < fds.size()) {
                fds[fd_count++] = received_fds[index];
            } else {
                ::close(received_fds[index]);
            }
        }
    }

    return receive_result{.bytes = static_cast<usize>(received), .fds = fd_count};
}

auto stream::send(std::span<const std::byte> data, std::span<const handle_t> fds) noexcept
    -> result<send_result> {
    if (fds.size() > max_fds) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "too many file descriptors"));
    }
    std::array<char, CMSG_SPACE(sizeof(handle_t) * max_fds)> control{};
    iovec vector{.iov_base = const_cast<std::byte*>(data.data()), .iov_len = data.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;

    if (!fds.empty()) {
        message.msg_control = control.data();
        message.msg_controllen = CMSG_SPACE(sizeof(handle_t) * fds.size());
        cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(handle_t) * fds.size());
        std::memcpy(CMSG_DATA(header), fds.data(), sizeof(handle_t) * fds.size());
    }

    isize sent = -1;
    do {
        sent = ::sendmsg(fd_, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return send_result{.would_block = true};
        }
        return std::unexpected(io_error("sendmsg failed"));
    }
    return send_result{.bytes = static_cast<usize>(sent)};
}

auto stream::destroy() const noexcept -> void {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

listener::listener(listener&& other) noexcept : fd_(other.fd_), path_(std::move(other.path_)) {
    other.fd_ = -1;
}

listener::~listener() noexcept {
    destroy();
}

auto listener::operator=(listener&& other) noexcept -> listener& {
    if (this != &other) {
        destroy();
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        other.fd_ = -1;
    }
    return *this;
}

auto listener::create(std::string_view path) noexcept -> result<listener> {
    sockaddr_un address{};
    if (!make_address(path, address)) {
        return std::unexpected(make_error(error_code::invalid_argument, "socket path is invalid"));
    }

    const handle_t socket_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (socket_fd < 0) {
        return std::unexpected(io_error("socket failed"));
    }

    ::unlink(address.sun_path);
    if (::bind(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const error failure = io_error("bind failed");
        ::close(socket_fd);
        return std::unexpected(failure);
    }
    if (::listen(socket_fd, 128) != 0) {
        const error failure = io_error("listen failed");
        ::close(socket_fd);
        ::unlink(path.data());
        return std::unexpected(failure);
    }
    return listener{socket_fd, std::string{path}};
}

auto listener::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto listener::fd() const noexcept -> handle_t {
    return fd_;
}

auto listener::accept() noexcept -> result<stream> {
    handle_t client_fd = -1;
    do {
        client_fd = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    } while (client_fd < 0 && errno == EINTR);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::unexpected(make_error(error_code::cancelled, "accept would block"));
        }
        return std::unexpected(io_error("accept4 failed"));
    }
    return stream{client_fd};
}

auto listener::destroy() const noexcept -> void {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
        path_.clear();
    }
}

auto connect_stream(std::string_view path) noexcept -> result<stream> {
    sockaddr_un address{};
    if (!make_address(path, address)) {
        return std::unexpected(make_error(error_code::invalid_argument, "socket path is invalid"));
    }

    const handle_t socket_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0) {
        return std::unexpected(io_error("socket failed"));
    }
    if (::connect(socket_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const error failure = io_error("connect failed");
        ::close(socket_fd);
        return std::unexpected(failure);
    }
    if (!set_nonblocking(socket_fd)) {
        const error failure = io_error("fcntl failed");
        ::close(socket_fd);
        return std::unexpected(failure);
    }
    return stream{socket_fd};
}

}
