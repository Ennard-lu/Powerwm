// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/connection.hpp>
#include <wayland/core.hpp>
#include <wayland/object_table.hpp>
#include <wayland/xdg_shell.hpp>

#include <memory>
#include <optional>
#include <span>
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

struct data_source_event {
    object_id source = 0;
    u32 serial = 0;
    u32 actions = 0;
    std::span<const std::string> mime_types;
};

using data_source_callback = auto (*)(void* context, const data_source_event& event) -> void;
using data_source_destroyed_callback = auto (*)(void* context, object_id source) -> void;

struct data_offer_receive_event {
    object_id offer = 0;
    u32 source_client = 0;
    object_id source_object = 0;
    std::string_view mime_type;
    handle_t fd = -1;
};

using data_offer_receive_callback = auto (*)(void* context, const data_offer_receive_event& event)
    -> void;

struct data_offer_accept_event {
    object_id offer = 0;
    u32 source_client = 0;
    object_id source_object = 0;
    u32 serial = 0;
    std::string_view mime_type;
};

using data_offer_accept_callback = auto (*)(void* context, const data_offer_accept_event& event)
    -> void;

struct data_drag_event {
    object_id source = 0;
    object_id origin = 0;
    object_id icon = 0;
    u32 serial = 0;
};

using data_drag_callback = auto (*)(void* context, const data_drag_event& event) -> void;

struct data_offer_actions_event {
    object_id offer = 0;
    u32 source_client = 0;
    object_id source_object = 0;
    u32 source_actions = 0;
    u32 action = 0;
};

using data_offer_actions_callback = auto (*)(void* context, const data_offer_actions_event& event)
    -> void;

struct data_offer_finished_event {
    object_id offer = 0;
    u32 source_client = 0;
    object_id source_object = 0;
};

using data_offer_finished_callback = auto (*)(void* context, const data_offer_finished_event& event)
    -> void;

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

    auto set_data_source_callback(data_source_callback callback, void* context) noexcept -> void;

    auto set_data_source_destroyed_callback(data_source_destroyed_callback callback,
                                            void* context) noexcept -> void;

    auto set_data_offer_receive_callback(data_offer_receive_callback callback,
                                         void* context) noexcept -> void;

    auto set_data_offer_accept_callback(data_offer_accept_callback callback, void* context) noexcept
        -> void;

    [[nodiscard]] auto notify_data_source(object_id source, u32 serial) -> result<void>;

    [[nodiscard]] auto notify_data_source_destroyed(object_id source) -> void;

    [[nodiscard]] auto data_source_mime_types(object_id source) const
        -> std::span<const std::string>;

    [[nodiscard]] auto data_source_actions(object_id source) const noexcept -> u32;

    [[nodiscard]] auto create_data_offer(u32 source_client, object_id source_object,
                                         std::span<const std::string> mime_types, u32 actions)
        -> result<object_id>;

    [[nodiscard]] auto send_selection(object_id offer) -> result<void>;

    [[nodiscard]] auto layer_geometry(object_id surface, u32 output_width, u32 output_height,
                                      i32 buffer_width, i32 buffer_height) const
        -> std::optional<layer_surface_geometry>;

    [[nodiscard]] auto receive_data_offer(object_id offer, u32 source_client,
                                          object_id source_object, std::string_view mime_type,
                                          handle_t fd) -> result<void>;

    [[nodiscard]] auto accept_data_offer(object_id offer, u32 source_client,
                                         object_id source_object, u32 serial,
                                         std::string_view mime_type) -> result<void>;

    [[nodiscard]] auto send_data_source_send(object_id source, std::string_view mime, handle_t fd)
        -> result<void>;

    [[nodiscard]] auto send_data_source_target(object_id source, std::string_view mime)
        -> result<void>;

    [[nodiscard]] auto send_data_source_cancelled(object_id source) -> result<void>;

    auto set_data_drag_callback(data_drag_callback callback, void* context) noexcept -> void;

    auto set_data_offer_actions_callback(data_offer_actions_callback callback,
                                         void* context) noexcept -> void;

    auto set_data_offer_finished_callback(data_offer_finished_callback callback,
                                          void* context) noexcept -> void;

    [[nodiscard]] auto start_data_drag(object_id source, object_id origin, object_id icon,
                                       u32 serial) -> result<void>;

    [[nodiscard]] auto data_offer_actions(object_id offer, u32 source_client,
                                          object_id source_object, u32 source_actions, u32 action)
        -> result<void>;

    [[nodiscard]] auto finish_data_offer(object_id offer, u32 source_client,
                                         object_id source_object) -> result<void>;

    [[nodiscard]] auto send_data_source_action(object_id source, u32 action) -> result<void>;

    [[nodiscard]] auto send_data_source_dnd_drop_performed(object_id source) -> result<void>;

    [[nodiscard]] auto send_data_source_dnd_finished(object_id source) -> result<void>;

    [[nodiscard]] auto send_data_offer_event(object_id offer) -> result<void>;

    [[nodiscard]] auto send_data_device_enter(u32 serial, object_id surface, f64 x, f64 y,
                                              object_id offer) -> result<void>;

    [[nodiscard]] auto send_data_device_leave() -> result<void>;

    [[nodiscard]] auto send_data_device_motion(u32 time_ms, f64 x, f64 y) -> result<void>;

    [[nodiscard]] auto send_data_device_drop() -> result<void>;

    auto destroy_data_offer(object_id offer) -> void;

  private:
    friend struct display_object;
    friend struct registry_object;

    connection* connection_;
    display_object display_;
    core_protocol core_;
    xdg_shell_protocol xdg_;
    presentation_feedback_callback presentation_callback_ = nullptr;
    void* presentation_context_ = nullptr;
    data_source_callback data_source_callback_ = nullptr;
    void* data_source_context_ = nullptr;
    data_source_destroyed_callback data_source_destroyed_callback_ = nullptr;
    void* data_source_destroyed_context_ = nullptr;
    data_offer_receive_callback data_offer_receive_callback_ = nullptr;
    void* data_offer_receive_context_ = nullptr;
    data_offer_accept_callback data_offer_accept_callback_ = nullptr;
    void* data_offer_accept_context_ = nullptr;
    data_drag_callback data_drag_callback_ = nullptr;
    void* data_drag_context_ = nullptr;
    data_offer_actions_callback data_offer_actions_callback_ = nullptr;
    void* data_offer_actions_context_ = nullptr;
    data_offer_finished_callback data_offer_finished_callback_ = nullptr;
    void* data_offer_finished_context_ = nullptr;
    std::vector<std::unique_ptr<registry_object>> registries_;
    std::vector<registry_global> globals_;
    u32 next_global_name_ = 1;
    u32 next_serial_ = 1;
};

}
