// SPDX-License-Identifier: Apache-2.0

#include <platform/event/timer_fd.hpp>

#include <unistd.h>

#include <cerrno>
#include <sys/timerfd.h>

namespace pwm::platform::event {

auto timer_fd::create() noexcept -> result<timer_fd> {
    const handle_t fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd < 0) {
        return std::unexpected(make_error(error_code::io, "timerfd_create failed"));
    }
    return timer_fd{fd};
}

auto timer_fd::valid() const noexcept -> bool {
    return fd_ >= 0;
}

auto timer_fd::fd() const noexcept -> handle_t {
    return fd_;
}

auto timer_fd::arm_ms(u32 milliseconds) noexcept -> result<void> {
    itimerspec specification{};
    specification.it_value.tv_sec = static_cast<time_t>(milliseconds / 1000);
    specification.it_value.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    if (::timerfd_settime(fd_, 0, &specification, nullptr) != 0) {
        return std::unexpected(make_error(error_code::io, "timerfd_settime failed"));
    }
    return {};
}

auto timer_fd::disarm() noexcept -> result<void> {
    itimerspec specification{};
    if (::timerfd_settime(fd_, 0, &specification, nullptr) != 0) {
        return std::unexpected(make_error(error_code::io, "timerfd_settime failed"));
    }
    return {};
}

auto timer_fd::read_expirations() noexcept -> result<u64> {
    u64 expirations = 0;
    pwm::isize read_count = 0;
    do {
        read_count = ::read(fd_, &expirations, sizeof(expirations));
    } while (read_count < 0 && errno == EINTR);

    if (read_count != static_cast<pwm::isize>(sizeof(expirations))) {
        return std::unexpected(make_error(error_code::io, "timerfd read failed"));
    }
    return expirations;
}

auto timer_fd::destroy() const noexcept -> void {
    if (fd_ >= 0) {
        ::close(fd_);
    }
    fd_ = -1;
}

}