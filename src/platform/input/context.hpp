// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <optional>
#include <string_view>

namespace pwm::platform::input {

enum class input_event_type : u8 {
    other,
    keyboard_key,
    pointer_motion,
    pointer_button,
    pointer_axis,
    touch_down,
    touch_up,
    touch_motion,
};

struct input_event {
    input_event_type type = input_event_type::other;
    u32 time_ms = 0;
    u32 key_code = 0;
    u32 button = 0;
    bool pressed = false;
    f64 dx = 0.0;
    f64 dy = 0.0;
    f64 axis_value = 0.0;
    u8 axis = 0;
    u32 touch_slot = 0;
    f64 x = 0.0;
    f64 y = 0.0;
};

class context {
  public:
    context() noexcept = default;

    [[nodiscard]] static auto create(std::string_view seat_name) noexcept -> result<context>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto dispatch() noexcept -> result<void>;

    [[nodiscard]] auto next_event() noexcept -> std::optional<input_event>;

    auto destroy() const noexcept -> void;

  private:
    context(void* handle, void* udev, handle_t fd) noexcept
        : handle_(handle), udev_(udev), fd_(fd) {
    }

    mutable void* handle_ = nullptr;
    mutable void* udev_ = nullptr;
    mutable handle_t fd_ = -1;
};

}