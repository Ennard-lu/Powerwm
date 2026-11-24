// SPDX-License-Identifier: Apache-2.0

#include <platform/input/context.hpp>

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <unistd.h>

#include <string>

namespace {

auto open_restricted(const char* path, int flags, void* user_data) -> int {
    static_cast<void>(user_data);
    return ::open(path, flags | O_CLOEXEC);
}

auto close_restricted(int fd, void* user_data) -> void {
    static_cast<void>(user_data);
    ::close(fd);
}

constexpr libinput_interface interface{
    .open_restricted = open_restricted,
    .close_restricted = close_restricted,
};

auto map_event_type(libinput_event_type type) -> pwm::platform::input::input_event_type {
    using pwm::platform::input::input_event_type;
    switch (type) {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
        return input_event_type::keyboard_key;
    case LIBINPUT_EVENT_POINTER_MOTION:
        return input_event_type::pointer_motion;
    case LIBINPUT_EVENT_POINTER_BUTTON:
        return input_event_type::pointer_button;
    case LIBINPUT_EVENT_POINTER_AXIS:
        return input_event_type::pointer_axis;
    case LIBINPUT_EVENT_TOUCH_DOWN:
        return input_event_type::touch_down;
    case LIBINPUT_EVENT_TOUCH_UP:
        return input_event_type::touch_up;
    case LIBINPUT_EVENT_TOUCH_MOTION:
        return input_event_type::touch_motion;
    default:
        return input_event_type::other;
    }
}

auto read_keyboard_event(libinput_event* event, pwm::platform::input::input_event& result) -> void {
    auto* keyboard = libinput_event_get_keyboard_event(event);
    result.time_ms = libinput_event_keyboard_get_time(keyboard);
    result.key_code = libinput_event_keyboard_get_key(keyboard);
    result.pressed = libinput_event_keyboard_get_key_state(keyboard) == LIBINPUT_KEY_STATE_PRESSED;
}

auto read_pointer_event(libinput_event* event, pwm::platform::input::input_event& result) -> void {
    auto* pointer = libinput_event_get_pointer_event(event);
    result.time_ms = libinput_event_pointer_get_time(pointer);
    switch (libinput_event_get_type(event)) {
    case LIBINPUT_EVENT_POINTER_MOTION:
        result.dx = libinput_event_pointer_get_dx(pointer);
        result.dy = libinput_event_pointer_get_dy(pointer);
        break;
    case LIBINPUT_EVENT_POINTER_BUTTON:
        result.button = libinput_event_pointer_get_button(pointer);
        result.pressed =
            libinput_event_pointer_get_button_state(pointer) == LIBINPUT_BUTTON_STATE_PRESSED;
        break;
    case LIBINPUT_EVENT_POINTER_AXIS:
        result.axis_value =
            libinput_event_pointer_get_axis_value(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
        result.axis = 0;
        break;
    default:
        break;
    }
}

auto read_touch_event(libinput_event* event, pwm::platform::input::input_event& result) -> void {
    auto* touch = libinput_event_get_touch_event(event);
    result.time_ms = libinput_event_touch_get_time(touch);
    result.touch_slot = static_cast<pwm::u32>(libinput_event_touch_get_slot(touch));
    result.x = libinput_event_touch_get_x(touch);
    result.y = libinput_event_touch_get_y(touch);
}

auto read_event(libinput_event* event) -> pwm::platform::input::input_event {
    using pwm::platform::input::input_event;
    using pwm::platform::input::input_event_type;

    input_event result{};
    result.type = map_event_type(libinput_event_get_type(event));

    switch (result.type) {
    case input_event_type::keyboard_key:
        read_keyboard_event(event, result);
        break;
    case input_event_type::pointer_motion:
    case input_event_type::pointer_button:
    case input_event_type::pointer_axis:
        read_pointer_event(event, result);
        break;
    case input_event_type::touch_down:
    case input_event_type::touch_up:
    case input_event_type::touch_motion:
        read_touch_event(event, result);
        break;
    default:
        break;
    }

    return result;
}

}

namespace pwm::platform::input {

auto context::create(std::string_view seat_name) noexcept -> result<context> {
    udev* udev = udev_new();
    if (udev == nullptr) {
        return std::unexpected(make_error(error_code::resource_exhausted, "udev_new failed"));
    }

    libinput* input = libinput_udev_create_context(&interface, nullptr, udev);
    if (input == nullptr) {
        udev_unref(udev);
        return std::unexpected(make_error(error_code::io, "libinput_udev_create_context failed"));
    }

    const std::string seat{seat_name};
    if (libinput_udev_assign_seat(input, seat.c_str()) != 0) {
        libinput_unref(input);
        udev_unref(udev);
        return std::unexpected(make_error(error_code::io, "libinput_udev_assign_seat failed"));
    }

    return context{input, udev, libinput_get_fd(input)};
}

auto context::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto context::fd() const noexcept -> handle_t {
    return fd_;
}

auto context::dispatch() noexcept -> result<void> {
    if (libinput_dispatch(static_cast<libinput*>(handle_)) != 0) {
        return std::unexpected(make_error(error_code::io, "libinput_dispatch failed"));
    }
    return {};
}

auto context::next_event() noexcept -> std::optional<input_event> {
    libinput_event* event = libinput_get_event(static_cast<libinput*>(handle_));
    if (event == nullptr) {
        return std::nullopt;
    }

    const input_event result = read_event(event);
    libinput_event_destroy(event);
    return result;
}

auto context::destroy() const noexcept -> void {
    libinput_unref(static_cast<libinput*>(handle_));
    udev_unref(static_cast<udev*>(udev_));
    handle_ = nullptr;
    udev_ = nullptr;
    fd_ = -1;
}

}