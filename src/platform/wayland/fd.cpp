// SPDX-License-Identifier: Apache-2.0

#include <platform/wayland/fd.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace pwm::platform::wayland {

file_descriptor::file_descriptor(file_descriptor&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

file_descriptor::~file_descriptor() noexcept {
    destroy();
}

auto file_descriptor::operator=(file_descriptor&& other) noexcept -> file_descriptor& {
    if (this != &other) {
        destroy();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

auto file_descriptor::adopt(handle_t fd) noexcept -> file_descriptor {
    return file_descriptor{fd};
}

auto file_descriptor::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto file_descriptor::fd() const noexcept -> handle_t {
    return fd_;
}

auto file_descriptor::read(std::span<std::byte> buffer) const noexcept -> result<usize> {
    while (true) {
        const auto count = ::read(fd_, buffer.data(), buffer.size());
        if (count >= 0) {
            return static_cast<usize>(count);
        }
        if (errno != EINTR) {
            return std::unexpected(
                make_error(error_code::io, "read failed", std::source_location::current(), errno));
        }
    }
}

auto file_descriptor::write(std::span<const std::byte> buffer) const noexcept -> result<usize> {
    while (true) {
        const auto count = ::write(fd_, buffer.data(), buffer.size());
        if (count >= 0) {
            return static_cast<usize>(count);
        }
        if (errno != EINTR) {
            return std::unexpected(
                make_error(error_code::io, "write failed", std::source_location::current(), errno));
        }
    }
}

auto file_descriptor::destroy() const noexcept -> void {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

auto create_pipe() noexcept -> result<file_descriptor_pair> {
    int fds[2]{-1, -1};
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        return std::unexpected(
            make_error(error_code::io, "pipe2 failed", std::source_location::current(), errno));
    }
    return file_descriptor_pair{
        .read = file_descriptor::adopt(fds[0]),
        .write = file_descriptor::adopt(fds[1]),
    };
}

}
