// SPDX-License-Identifier: Apache-2.0

#include <platform/event/event_loop.hpp>

#include <poll.h>

#include <cerrno>

namespace {

auto to_poll_events(pwm::platform::event::poll_event events) -> short {
    short result = 0;
    const auto value = static_cast<pwm::u8>(events);
    if ((value & static_cast<pwm::u8>(pwm::platform::event::poll_event::readable)) != 0) {
        result |= POLLIN;
    }
    if ((value & static_cast<pwm::u8>(pwm::platform::event::poll_event::writable)) != 0) {
        result |= POLLOUT;
    }
    return result;
}

auto from_poll_events(short events) -> pwm::platform::event::poll_event {
    auto result = static_cast<pwm::u8>(0);
    if ((events & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) != 0) {
        result |= static_cast<pwm::u8>(pwm::platform::event::poll_event::readable);
    }
    if ((events & POLLOUT) != 0) {
        result |= static_cast<pwm::u8>(pwm::platform::event::poll_event::writable);
    }
    if ((events & (POLLERR | POLLNVAL)) != 0) {
        result |= static_cast<pwm::u8>(pwm::platform::event::poll_event::error);
    }
    return static_cast<pwm::platform::event::poll_event>(result);
}

}

namespace pwm::platform::event {

auto event_loop::add(handle_t fd, poll_event events, poll_callback callback, void* context) noexcept
    -> result<void> {
    if (fd < 0 || callback == nullptr) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "invalid event registration"));
    }
    for (const auto& registration : registrations_) {
        if (registration.fd == fd) {
            return std::unexpected(
                make_error(error_code::invalid_argument, "fd is already registered"));
        }
    }
    registrations_.push_back(
        {.fd = fd, .events = events, .callback = callback, .context = context});
    return {};
}

auto event_loop::remove(handle_t fd) noexcept -> result<void> {
    for (auto iterator = registrations_.begin(); iterator != registrations_.end(); ++iterator) {
        if (iterator->fd == fd) {
            registrations_.erase(iterator);
            return {};
        }
    }
    return std::unexpected(make_error(error_code::not_found, "fd is not registered"));
}

auto event_loop::run_once(i32 timeout_ms) noexcept -> result<i32> {
    if (registrations_.empty()) {
        return 0;
    }

    const auto snapshot = registrations_;
    std::vector<pollfd> descriptors;
    descriptors.reserve(snapshot.size());
    for (const auto& registration : snapshot) {
        descriptors.push_back({
            .fd = registration.fd,
            .events = to_poll_events(registration.events),
            .revents = 0,
        });
    }

    const i32 result = ::poll(descriptors.data(), descriptors.size(), timeout_ms);
    if (result < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return std::unexpected(make_error(error_code::io, "poll failed"));
    }
    if (result == 0) {
        return 0;
    }

    i32 dispatched = 0;
    for (usize index = 0; index < snapshot.size(); ++index) {
        if (descriptors[index].revents == 0) {
            continue;
        }
        snapshot[index].callback(snapshot[index].context,
                                 from_poll_events(descriptors[index].revents));
        ++dispatched;
    }
    return dispatched;
}

auto event_loop::destroy() noexcept -> void {
    registrations_.clear();
}

}