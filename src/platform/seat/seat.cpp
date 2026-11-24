// SPDX-License-Identifier: Apache-2.0

#include <platform/seat/seat.hpp>

extern "C" {
#include <libseat.h>
}

#include <memory>
#include <string>

namespace {

struct seat_state {
    bool enabled = false;
};

auto enable_seat(libseat*, void* user_data) -> void {
    static_cast<seat_state*>(user_data)->enabled = true;
}

auto disable_seat(libseat*, void* user_data) -> void {
    static_cast<seat_state*>(user_data)->enabled = false;
}

constexpr libseat_seat_listener listener{
    .enable_seat = enable_seat,
    .disable_seat = disable_seat,
};

}

namespace pwm::platform::seat {

auto seat_device::valid() const noexcept -> bool {
    return seat_handle_ != nullptr && fd_ >= 0 && device_id_ >= 0;
}

auto seat_device::fd() const noexcept -> handle_t {
    return fd_;
}

auto seat_device::device_id() const noexcept -> i32 {
    return device_id_;
}

auto seat_device::destroy() const noexcept -> void {
    libseat_close_device(static_cast<libseat*>(seat_handle_), device_id_);
    seat_handle_ = nullptr;
    fd_ = -1;
    device_id_ = -1;
}

auto session::create() noexcept -> result<session> {
    auto state = std::make_unique<seat_state>();
    libseat* handle = libseat_open_seat(&listener, state.get());
    if (handle == nullptr) {
        return std::unexpected(make_error(error_code::io, "libseat_open_seat failed"));
    }
    return session{handle, state.release()};
}

auto session::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto session::is_enabled() const noexcept -> bool {
    return static_cast<const seat_state*>(state_)->enabled;
}

auto session::open_device(std::string_view path) noexcept -> result<seat_device> {
    const std::string device_path{path};
    handle_t fd = -1;
    const i32 device_id =
        libseat_open_device(static_cast<libseat*>(handle_), device_path.c_str(), &fd);
    if (device_id < 0 || fd < 0) {
        return std::unexpected(make_error(error_code::io, "libseat_open_device failed"));
    }
    return seat_device{handle_, fd, device_id};
}

auto session::dispatch(i32 timeout_ms) noexcept -> result<void> {
    if (libseat_dispatch(static_cast<libseat*>(handle_), timeout_ms) < 0) {
        return std::unexpected(make_error(error_code::io, "libseat_dispatch failed"));
    }
    return {};
}

auto session::destroy() const noexcept -> void {
    libseat_close_seat(static_cast<libseat*>(handle_));
    std::unique_ptr<seat_state> state{static_cast<seat_state*>(state_)};
    handle_ = nullptr;
    state_ = nullptr;
}

}