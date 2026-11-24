// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/connection.hpp>
#include <wayland/core.hpp>
#include <wayland/object_table.hpp>
#include <wayland/xdg_shell.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pwm::wayland {

struct registry_global {
    u32 name = 0;
    std::string interface;
    u32 version = 1;
};

class display_service;

struct presentation_feedback_event {
    object_id feedback = 0;
    object_id surface = 0;
};

using presentation_feedback_callback = auto (*)(void* context,
                                                const presentation_feedback_event& event) -> void;

struct display_object {
    display_service* service = nullptr;

    [[nodiscard]] auto request_sync(object_id callback) -> result<void>;
    [[nodiscard]] auto request_get_registry(object_id registry) -> result<void>;
};

struct registry_object {
    display_service* service = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_bind(u32 name, std::string_view interface, u32 version,
                                    object_id new_id) -> result<void>;
};

class display_service {
  public:
    explicit display_service(connection& connection) noexcept;

    [[nodiscard]] auto start() -> result<void>;

    [[nodiscard]] auto add_global(std::string_view interface, u32 version) -> u32;

    auto advertise_core(u32 output_width, u32 output_height, u32 refresh_hz) -> void;

    auto configure_output(u32 width, u32 height, u32 refresh_hz) -> void;

    [[nodiscard]] auto globals() const noexcept -> const std::vector<registry_global>&;

    [[nodiscard]] auto next_serial() noexcept -> u32;

    [[nodiscard]] auto send_registry_globals(object_id registry) -> result<void>;

    [[nodiscard]] auto connection_ref() noexcept -> connection&;

    [[nodiscard]] auto queue_event(const protocol_writer& writer) -> result<void>;

    [[nodiscard]] auto destroy_object(object_id id) -> result<void>;

    [[nodiscard]] auto bind_global(std::string_view interface, object_id id, u32 version)
        -> result<void>;

    [[nodiscard]] auto send_callback_done(object_id callback, u32 data) -> result<void>;

    [[nodiscard]] auto send_delete_id(object_id id) -> result<void>;

    [[nodiscard]] auto keyboards() const noexcept -> std::span<const object_id>;

    [[nodiscard]] auto send_keyboard_enter(object_id surface) -> result<void>;

    [[nodiscard]] auto send_keyboard_key(u32 time_ms, u32 key, bool pressed) -> result<void>;

    [[nodiscard]] auto send_keyboard_modifiers(u32 depressed, u32 latched, u32 locked, u32 group)
        -> result<void>;

    [[nodiscard]] auto send_pointer_enter(object_id surface, f64 x, f64 y) -> result<void>;

    [[nodiscard]] auto send_pointer_leave(object_id surface) -> result<void>;

    [[nodiscard]] auto send_pointer_motion(u32 time_ms, f64 x, f64 y) -> result<void>;

    [[nodiscard]] auto send_pointer_button(u32 time_ms, u32 button, bool pressed) -> result<void>;

    [[nodiscard]] auto send_pointer_axis(u32 time_ms, u32 axis, f64 value) -> result<void>;

    [[nodiscard]] auto send_pointer_frame() -> result<void>;

    [[nodiscard]] auto send_touch_down(object_id surface, u32 time_ms, i32 id, f64 x, f64 y)
        -> result<void>;

    [[nodiscard]] auto send_touch_up(u32 time_ms, i32 id) -> result<void>;

    [[nodiscard]] auto send_touch_motion(u32 time_ms, i32 id, f64 x, f64 y) -> result<void>;

    [[nodiscard]] auto send_touch_frame() -> result<void>;

    auto set_surface_commit_callback(surface_commit_callback callback, void* context) noexcept
        -> void;

    auto set_presentation_feedback_callback(presentation_feedback_callback callback,
                                            void* context) noexcept -> void;

    [[nodiscard]] auto request_presentation_feedback(object_id feedback, object_id surface)
        -> result<void>;

    [[nodiscard]] auto send_presentation_feedback(object_id feedback, u32 seconds_low,
                                                  u32 nanoseconds, u32 refresh_ns) -> result<void>;

  private:
    friend struct display_object;
    friend struct registry_object;

    connection* connection_;
    display_object display_;
    core_protocol core_;
    xdg_shell_protocol xdg_;
    presentation_feedback_callback presentation_callback_ = nullptr;
    void* presentation_context_ = nullptr;
    std::vector<std::unique_ptr<registry_object>> registries_;
    std::vector<registry_global> globals_;
    u32 next_global_name_ = 1;
    u32 next_serial_ = 1;
};

}
