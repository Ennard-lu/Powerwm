// SPDX-License-Identifier: Apache-2.0

#include <platform/event/poll.hpp>

#include <poll.h>

#include <cerrno>

namespace pwm::platform::event {

auto wait_readable(handle_t fd, i32 timeout_ms) noexcept -> result<bool> {
    pollfd descriptor{
        .fd = fd,
        .events = POLLIN,
        .revents = 0,
    };

    while (true) {
        const i32 result = ::poll(&descriptor, 1, timeout_ms);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected(make_error(error_code::io, "poll failed"));
        }
        if (result == 0) {
            return false;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return std::unexpected(make_error(error_code::io, "poll reported an fd error"));
        }
        return (descriptor.revents & POLLIN) != 0;
    }
}

}