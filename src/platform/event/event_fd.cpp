// SPDX-License-Identifier: Apache-2.0

#include <platform/event/event_fd.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <sys/eventfd.h>

namespace pwm::platform::event {

auto event_fd::create(u32 initial_value) noexcept -> result<event_fd> {
    const handle_t fd = ::eventfd(initial_value, EFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(make_error(error_code::io, "eventfd failed"));
    }
    return event_fd{fd};
}

auto event_fd::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto event_fd::fd() const noexcept -> handle_t {
    return fd_;
}

auto event_fd::write(u64 value) noexcept -> result<void> {
    eventfd_t payload = value;
    pwm::isize written = 0;
    do {
        written = ::write(fd_, &payload, sizeof(payload));
    } while (written < 0 && errno == EINTR);

    if (written != static_cast<pwm::isize>(sizeof(payload))) {
        return std::unexpected(make_error(error_code::io, "eventfd write failed"));
    }
    return {};
}

auto event_fd::read() noexcept -> result<u64> {
    eventfd_t payload = 0;
    pwm::isize read_count = 0;
    do {
        read_count = ::read(fd_, &payload, sizeof(payload));
    } while (read_count < 0 && errno == EINTR);

    if (read_count != static_cast<pwm::isize>(sizeof(payload))) {
        return std::unexpected(make_error(error_code::io, "eventfd read failed"));
    }
    return payload;
}

auto event_fd::destroy() const noexcept -> void {
    ::close(fd_);
    fd_ = -1;
}

}