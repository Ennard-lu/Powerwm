// SPDX-License-Identifier: Apache-2.0

#include <wayland/display.hpp>

#include <chrono>
#include <memory>
#include <presentation_time_bindings.hpp>
#include <tuple>
#include <utility>
#include <wayland_bindings.hpp>

namespace pwm::wayland {

display_service::display_service(connection& connection) noexcept
    : connection_(&connection), display_{this}, core_{*this}, xdg_{*this} {
}

auto display_service::start() -> result<void> {
    return add_object<display_object, generated::wl_display>(connection_->objects(), 1, display_);
}

auto display_service::add_global(std::string_view interface, u32 version) -> u32 {
    const u32 name = next_global_name_++;
    globals_.push_back(registry_global{
        .name = name,
        .interface = std::string{interface},
        .version = version,
    });
    return name;
}

auto display_service::announce_global(u32 name) -> result<void> {
    for (const auto& global : globals_) {
        if (global.name != name) {
            continue;
        }
        for (const auto& registry : registries_) {
            generated::wl_registry::event_global event{};
            std::get<0>(event.arguments) = global.name;
            std::get<1>(event.arguments) = global.interface;
            std::get<2>(event.arguments) = global.version;
            protocol_writer writer;
            if (auto encoded =
                    generated::wl_registry::event_global::encode(writer, registry->id, event);
                !encoded) {
                return std::unexpected(encoded.error());
            }
            if (auto queued = queue_event(writer); !queued) {
                return std::unexpected(queued.error());
            }
        }
        return {};
    }
    return std::unexpected(make_error(error_code::not_found, "global is unknown"));
}

auto display_service::advertise_core(u32 output_width, u32 output_height, u32 refresh_hz) -> void {
    core_.configure_output(output_width, output_height, refresh_hz);
    add_global("wl_compositor", 4);
    add_global("wl_subcompositor", 1);
    add_global("wl_shm", 1);
    add_global("wl_output", 4);
    add_global("zxdg_output_manager_v1", 3);
    add_global("wp_viewporter", 1);
    add_global("wp_fractional_scale_manager_v1", 1);
    add_global("zxdg_decoration_manager_v1", 1);

    add_global("xdg_activation_v1", 1);
    add_global("wl_seat", 2);
    add_global("wl_data_device_manager", 3);
    add_global("zwp_linux_dmabuf_v1", 3);
    add_global("zwlr_layer_shell_v1", 4);
    add_global("zwp_primary_selection_device_manager_v1", 1);
    add_global("xdg_wm_base", 1);
}

auto display_service::configure_output(u32 width, u32 height, u32 refresh_hz) -> void {
    core_.configure_output(width, height, refresh_hz);
}

auto display_service::configure_outputs(std::span<const output_descriptor> outputs) -> void {
    core_.configure_outputs(outputs);
}

auto display_service::output_descriptor_for(object_id id) const
    -> std::optional<output_descriptor> {
    return core_.output_descriptor_for(id);
}

auto display_service::globals() const noexcept -> const std::vector<registry_global>& {
    return globals_;
}

auto display_service::connection_ref() noexcept -> connection& {
    return *connection_;
}

auto display_service::queue_event(const protocol_writer& writer) -> result<void> {
    return connection_->queue_message(writer);
}

auto display_service::destroy_object(object_id id) -> result<void> {
    if (!connection_->objects().remove(id)) {
        return std::unexpected(
            make_error(error_code::protocol, "destroy references an unknown object"));
    }
    queue_discard(id);
    return send_delete_id(id);
}

auto display_service::queue_discard(object_id id) noexcept -> void {
    pending_discards_.push_back(id);
}

auto display_service::process_discards() noexcept -> void {
    for (const object_id id : pending_discards_) {
        core_.discard_object(id);
        xdg_.discard_object(id);
    }
    pending_discards_.clear();
}

auto display_service::bind_global(std::string_view interface, object_id id, u32 version)
    -> result<void> {
    if (interface == "xdg_wm_base") {
        return xdg_.bind_global(interface, id, version);
    }
    return core_.bind_global(interface, id, version);
}

auto display_service::next_serial() noexcept -> u32 {
    return ++next_serial_;
}

auto display_service::send_registry_globals(object_id registry) -> result<void> {
    for (const auto& global : globals_) {
        generated::wl_registry::event_global event{};
        std::get<0>(event.arguments) = global.name;
        std::get<1>(event.arguments) = global.interface;
        std::get<2>(event.arguments) = global.version;

        protocol_writer writer;
        if (auto encoded = generated::wl_registry::event_global::encode(writer, registry, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::keyboards() const noexcept -> std::span<const object_id> {
    return core_.keyboards();
}

auto display_service::send_keyboard_enter(object_id surface) -> result<void> {
    for (const object_id keyboard : core_.keyboards()) {
        generated::wl_keyboard::event_enter event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = surface;
        protocol_writer writer;
        if (auto encoded = generated::wl_keyboard::event_enter::encode(writer, keyboard, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_keyboard_key(u32 time_ms, u32 key, bool pressed) -> result<void> {
    for (const object_id keyboard : core_.keyboards()) {
        generated::wl_keyboard::event_key event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = time_ms;
        std::get<2>(event.arguments) = key;
        std::get<3>(event.arguments) = pressed ? 1U : 0U;
        protocol_writer writer;
        if (auto encoded = generated::wl_keyboard::event_key::encode(writer, keyboard, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_keyboard_modifiers(u32 depressed, u32 latched, u32 locked, u32 group)
    -> result<void> {
    for (const object_id keyboard : core_.keyboards()) {
        generated::wl_keyboard::event_modifiers event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = depressed;
        std::get<2>(event.arguments) = latched;
        std::get<3>(event.arguments) = locked;
        std::get<4>(event.arguments) = group;
        protocol_writer writer;
        if (auto encoded = generated::wl_keyboard::event_modifiers::encode(writer, keyboard, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_enter(object_id surface, f64 x, f64 y) -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_enter event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = surface;
        std::get<2>(event.arguments) = x;
        std::get<3>(event.arguments) = y;
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_enter::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_leave(object_id surface) -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_leave event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = surface;
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_leave::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_motion(u32 time_ms, f64 x, f64 y) -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_motion event{};
        std::get<0>(event.arguments) = time_ms;
        std::get<1>(event.arguments) = x;
        std::get<2>(event.arguments) = y;
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_motion::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_button(u32 time_ms, u32 button, bool pressed) -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_button event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = time_ms;
        std::get<2>(event.arguments) = button;
        std::get<3>(event.arguments) = pressed ? 1U : 0U;
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_button::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_axis(u32 time_ms, u32 axis, f64 value) -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_axis event{};
        std::get<0>(event.arguments) = time_ms;
        std::get<1>(event.arguments) = axis;
        std::get<2>(event.arguments) = value;
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_axis::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_pointer_frame() -> result<void> {
    for (const object_id pointer : core_.pointers()) {
        generated::wl_pointer::event_frame event{};
        protocol_writer writer;
        if (auto encoded = generated::wl_pointer::event_frame::encode(writer, pointer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_touch_down(object_id surface, u32 time_ms, i32 id, f64 x, f64 y)
    -> result<void> {
    for (const object_id touch : core_.touches()) {
        generated::wl_touch::event_down event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = time_ms;
        std::get<2>(event.arguments) = surface;
        std::get<3>(event.arguments) = id;
        std::get<4>(event.arguments) = x;
        std::get<5>(event.arguments) = y;
        protocol_writer writer;
        if (auto encoded = generated::wl_touch::event_down::encode(writer, touch, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_touch_up(u32 time_ms, i32 id) -> result<void> {
    for (const object_id touch : core_.touches()) {
        generated::wl_touch::event_up event{};
        std::get<0>(event.arguments) = next_serial();
        std::get<1>(event.arguments) = time_ms;
        std::get<2>(event.arguments) = id;
        protocol_writer writer;
        if (auto encoded = generated::wl_touch::event_up::encode(writer, touch, event); !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_touch_motion(u32 time_ms, i32 id, f64 x, f64 y) -> result<void> {
    for (const object_id touch : core_.touches()) {
        generated::wl_touch::event_motion event{};
        std::get<0>(event.arguments) = time_ms;
        std::get<1>(event.arguments) = id;
        std::get<2>(event.arguments) = x;
        std::get<3>(event.arguments) = y;
        protocol_writer writer;
        if (auto encoded = generated::wl_touch::event_motion::encode(writer, touch, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_touch_frame() -> result<void> {
    for (const object_id touch : core_.touches()) {
        generated::wl_touch::event_frame event{};
        protocol_writer writer;
        if (auto encoded = generated::wl_touch::event_frame::encode(writer, touch, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::set_surface_commit_callback(surface_commit_callback callback,
                                                  void* context) noexcept -> void {
    core_.set_surface_commit_callback(callback, context);
}

auto display_service::set_presentation_feedback_callback(presentation_feedback_callback callback,
                                                         void* context) noexcept -> void {
    presentation_callback_ = callback;
    presentation_context_ = context;
}

auto display_service::request_presentation_feedback(object_id feedback, object_id surface)
    -> result<void> {
    if (presentation_callback_ != nullptr) {
        presentation_callback_(presentation_context_, {feedback, surface});
        return {};
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return send_presentation_feedback(feedback, static_cast<u32>(seconds), 0, 16666667);
}

auto display_service::send_presentation_feedback(object_id feedback, u32 seconds_low,
                                                 u32 nanoseconds, u32 refresh_ns) -> result<void> {
    generated::wp_presentation_feedback::event_presented event{};
    std::get<0>(event.arguments) = 0;
    std::get<1>(event.arguments) = seconds_low;
    std::get<2>(event.arguments) = nanoseconds;
    std::get<3>(event.arguments) = refresh_ns;
    std::get<4>(event.arguments) = 0;
    std::get<5>(event.arguments) = 0;
    std::get<6>(event.arguments) = 0;
    protocol_writer writer;
    if (auto encoded =
            generated::wp_presentation_feedback::event_presented::encode(writer, feedback, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return queue_event(writer);
}

auto display_service::set_data_source_callback(data_source_callback callback,
                                               void* context) noexcept -> void {
    data_source_callback_ = callback;
    data_source_context_ = context;
}

auto display_service::set_data_source_destroyed_callback(data_source_destroyed_callback callback,
                                                         void* context) noexcept -> void {
    data_source_destroyed_callback_ = callback;
    data_source_destroyed_context_ = context;
}

auto display_service::set_data_offer_receive_callback(data_offer_receive_callback callback,
                                                      void* context) noexcept -> void {
    data_offer_receive_callback_ = callback;
    data_offer_receive_context_ = context;
}

auto display_service::set_data_offer_accept_callback(data_offer_accept_callback callback,
                                                     void* context) noexcept -> void {
    data_offer_accept_callback_ = callback;
    data_offer_accept_context_ = context;
}

auto display_service::notify_data_source(object_id source, u32 serial) -> result<void> {
    if (data_source_callback_ == nullptr) {
        return {};
    }
    data_source_callback_(data_source_context_,
                          {.source = source,
                           .serial = serial,
                           .actions = core_.data_source_actions(source),
                           .mime_types = core_.data_source_mime_types(source)});
    return {};
}

auto display_service::notify_data_source_destroyed(object_id source) -> void {
    if (data_source_destroyed_callback_ != nullptr) {
        data_source_destroyed_callback_(data_source_destroyed_context_, source);
    }
}

auto display_service::set_primary_source_callback(primary_source_callback callback,
                                                  void* context) noexcept -> void {
    primary_source_callback_ = callback;
    primary_source_context_ = context;
}

auto display_service::set_primary_source_destroyed_callback(
    primary_source_destroyed_callback callback, void* context) noexcept -> void {
    primary_source_destroyed_callback_ = callback;
    primary_source_destroyed_context_ = context;
}

auto display_service::set_primary_offer_receive_callback(primary_offer_receive_callback callback,
                                                         void* context) noexcept -> void {
    primary_offer_receive_callback_ = callback;
    primary_offer_receive_context_ = context;
}

auto display_service::notify_primary_selection(object_id source, u32 serial) -> result<void> {
    if (primary_source_callback_ == nullptr) {
        return {};
    }
    primary_source_callback_(primary_source_context_,
                             {.source = source,
                              .serial = serial,
                              .mime_types = core_.primary_source_mime_types(source)});
    return {};
}

auto display_service::notify_primary_source_destroyed(object_id source) -> void {
    if (primary_source_destroyed_callback_ != nullptr) {
        primary_source_destroyed_callback_(primary_source_destroyed_context_, source);
    }
}

auto display_service::primary_source_mime_types(object_id source) const
    -> std::span<const std::string> {
    return core_.primary_source_mime_types(source);
}

auto display_service::create_primary_offer(u32 source_client, object_id source_object,
                                           std::span<const std::string> mime_types)
    -> result<object_id> {
    return core_.create_primary_offer(source_client, source_object, mime_types);
}

auto display_service::send_primary_selection(object_id offer) -> result<void> {
    return core_.send_primary_selection(offer);
}

auto display_service::receive_primary_offer(object_id offer, u32 source_client,
                                            object_id source_object, std::string_view mime_type,
                                            handle_t fd) -> result<void> {
    if (primary_offer_receive_callback_ == nullptr) {
        return {};
    }
    primary_offer_receive_callback_(primary_offer_receive_context_, {.offer = offer,
                                                                     .source_client = source_client,
                                                                     .source_object = source_object,
                                                                     .mime_type = mime_type,
                                                                     .fd = fd});
    return {};
}

auto display_service::send_primary_source_send(object_id source, std::string_view mime, handle_t fd)
    -> result<void> {
    return core_.send_primary_source_send(source, mime, fd);
}

auto display_service::send_primary_source_cancelled(object_id source) -> result<void> {
    return core_.send_primary_source_cancelled(source);
}

auto display_service::data_source_mime_types(object_id source) const
    -> std::span<const std::string> {
    return core_.data_source_mime_types(source);
}

auto display_service::data_source_actions(object_id source) const noexcept -> u32 {
    return core_.data_source_actions(source);
}

auto display_service::create_data_offer(u32 source_client, object_id source_object,
                                        std::span<const std::string> mime_types, u32 actions)
    -> result<object_id> {
    return core_.create_data_offer(source_client, source_object, mime_types, actions);
}

auto display_service::send_selection(object_id offer) -> result<void> {
    return core_.send_selection(offer);
}

auto display_service::set_defer_frame_callbacks(bool value) noexcept -> void {
    core_.set_defer_frame_callbacks(value);
}

auto display_service::flush_frame_callbacks() -> result<void> {
    return core_.flush_frame_callbacks();
}

auto display_service::viewport_destination(object_id surface) const -> std::optional<base::recti> {
    return core_.viewport_destination(surface);
}

auto display_service::surface_popup_geometry(object_id surface) const
    -> std::optional<popup_geometry> {
    return xdg_.surface_popup_geometry(surface);
}

auto display_service::dismiss_grabbed_popups(object_id hit_surface) -> void {
    xdg_.dismiss_grabbed_popups(hit_surface);
}

auto display_service::surface_buffer_scale(object_id surface) const noexcept -> i32 {
    return core_.surface_buffer_scale(surface);
}

auto display_service::layer_geometry(object_id surface, u32 output_width, u32 output_height,
                                     i32 buffer_width, i32 buffer_height) const
    -> std::optional<layer_surface_geometry> {
    return core_.layer_geometry(surface, output_width, output_height, buffer_width, buffer_height);
}

auto display_service::receive_data_offer(object_id offer, u32 source_client,
                                         object_id source_object, std::string_view mime_type,
                                         handle_t fd) -> result<void> {
    if (data_offer_receive_callback_ == nullptr) {
        return {};
    }
    data_offer_receive_callback_(data_offer_receive_context_, {.offer = offer,
                                                               .source_client = source_client,
                                                               .source_object = source_object,
                                                               .mime_type = mime_type,
                                                               .fd = fd});
    return {};
}

auto display_service::accept_data_offer(object_id offer, u32 source_client, object_id source_object,
                                        u32 serial, std::string_view mime_type) -> result<void> {
    if (data_offer_accept_callback_ == nullptr) {
        return {};
    }
    data_offer_accept_callback_(data_offer_accept_context_, {.offer = offer,
                                                             .source_client = source_client,
                                                             .source_object = source_object,
                                                             .serial = serial,
                                                             .mime_type = mime_type});
    return {};
}

auto display_service::send_data_source_send(object_id source, std::string_view mime, handle_t fd)
    -> result<void> {
    return core_.send_data_source_send(source, mime, fd);
}

auto display_service::send_data_source_target(object_id source, std::string_view mime)
    -> result<void> {
    return core_.send_data_source_target(source, mime);
}

auto display_service::send_data_source_cancelled(object_id source) -> result<void> {
    return core_.send_data_source_cancelled(source);
}

auto display_service::set_data_drag_callback(data_drag_callback callback, void* context) noexcept
    -> void {
    data_drag_callback_ = callback;
    data_drag_context_ = context;
}

auto display_service::set_data_offer_actions_callback(data_offer_actions_callback callback,
                                                      void* context) noexcept -> void {
    data_offer_actions_callback_ = callback;
    data_offer_actions_context_ = context;
}

auto display_service::set_data_offer_finished_callback(data_offer_finished_callback callback,
                                                       void* context) noexcept -> void {
    data_offer_finished_callback_ = callback;
    data_offer_finished_context_ = context;
}

auto display_service::start_data_drag(object_id source, object_id origin, object_id icon,
                                      u32 serial) -> result<void> {
    if (data_drag_callback_ == nullptr) {
        return {};
    }
    data_drag_callback_(data_drag_context_,
                        {.source = source, .origin = origin, .icon = icon, .serial = serial});
    return {};
}

auto display_service::data_offer_actions(object_id offer, u32 source_client,
                                         object_id source_object, u32 source_actions, u32 action)
    -> result<void> {
    if (data_offer_actions_callback_ == nullptr) {
        return {};
    }
    data_offer_actions_callback_(data_offer_actions_context_, {.offer = offer,
                                                               .source_client = source_client,
                                                               .source_object = source_object,
                                                               .source_actions = source_actions,
                                                               .action = action});
    return {};
}

auto display_service::finish_data_offer(object_id offer, u32 source_client, object_id source_object)
    -> result<void> {
    if (data_offer_finished_callback_ == nullptr) {
        return {};
    }
    data_offer_finished_callback_(
        data_offer_finished_context_,
        {.offer = offer, .source_client = source_client, .source_object = source_object});
    return {};
}

auto display_service::send_data_source_action(object_id source, u32 action) -> result<void> {
    return core_.send_data_source_action(source, action);
}

auto display_service::send_data_source_dnd_drop_performed(object_id source) -> result<void> {
    return core_.send_data_source_dnd_drop_performed(source);
}

auto display_service::send_data_source_dnd_finished(object_id source) -> result<void> {
    return core_.send_data_source_dnd_finished(source);
}

auto display_service::send_data_offer_event(object_id offer) -> result<void> {
    return core_.send_data_offer_event(offer);
}

auto display_service::send_data_device_enter(u32 serial, object_id surface, f64 x, f64 y,
                                             object_id offer) -> result<void> {
    return core_.send_data_device_enter(serial, surface, x, y, offer);
}

auto display_service::send_data_device_leave() -> result<void> {
    return core_.send_data_device_leave();
}

auto display_service::send_data_device_motion(u32 time_ms, f64 x, f64 y) -> result<void> {
    return core_.send_data_device_motion(time_ms, x, y);
}

auto display_service::send_data_device_drop() -> result<void> {
    return core_.send_data_device_drop();
}

auto display_service::destroy_data_offer(object_id offer) -> void {
    core_.destroy_data_offer(offer);
}

auto display_service::send_callback_done(object_id callback, u32 data) -> result<void> {
    generated::wl_callback::event_done done{};
    std::get<0>(done.arguments) = data;

    protocol_writer writer;
    if (auto encoded = generated::wl_callback::event_done::encode(writer, callback, done);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = queue_event(writer); !queued) {
        return std::unexpected(queued.error());
    }
    return send_delete_id(callback);
}

auto display_service::send_delete_id(object_id id) -> result<void> {
    generated::wl_display::event_delete_id deleted{};
    std::get<0>(deleted.arguments) = id;

    protocol_writer writer;
    if (auto encoded = generated::wl_display::event_delete_id::encode(writer, 1, deleted);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return queue_event(writer);
}

auto display_object::request_sync(object_id callback) -> result<void> {
    return service->send_callback_done(callback, service->next_serial());
}

auto display_object::request_get_registry(object_id registry) -> result<void> {
    auto object = std::make_unique<registry_object>(registry_object{service, registry});
    if (auto added = add_object<registry_object, generated::wl_registry>(
            service->connection_->objects(), registry, *object);
        !added) {
        return std::unexpected(added.error());
    }
    service->registries_.push_back(std::move(object));
    return service->send_registry_globals(registry);
}

auto registry_object::request_bind(u32 name, std::string_view interface, u32 version,
                                   object_id new_id) -> result<void> {
    for (const auto& global : service->globals_) {
        if (global.name != name) {
            continue;
        }
        if (global.interface != interface) {
            return std::unexpected(
                make_error(error_code::protocol, "registry bind interface does not match"));
        }
        if (version == 0 || version > global.version) {
            return std::unexpected(
                make_error(error_code::protocol, "registry bind version is out of range"));
        }
        return service->bind_global(interface, new_id, version);
    }
    return std::unexpected(
        make_error(error_code::protocol, "registry bind references an unknown global"));
}

}
