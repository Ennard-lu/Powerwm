// SPDX-License-Identifier: Apache-2.0

#include <wayland/display.hpp>

#include <memory>
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

auto display_service::advertise_core(u32 output_width, u32 output_height, u32 refresh_hz) -> void {
    core_.configure_output(output_width, output_height, refresh_hz);
    add_global("wl_compositor", 4);
    add_global("wl_subcompositor", 1);
    add_global("wl_shm", 1);
    add_global("wl_output", 4);
    add_global("wl_seat", 2);
    add_global("wl_data_device_manager", 3);
    add_global("xdg_wm_base", 1);
}

auto display_service::configure_output(u32 width, u32 height, u32 refresh_hz) -> void {
    core_.configure_output(width, height, refresh_hz);
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
    return send_delete_id(id);
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
        if (global.name == name && global.interface == interface) {
            return service->bind_global(interface, new_id, version);
        }
    }
    return std::unexpected(
        make_error(error_code::protocol, "registry bind references an unknown global"));
}

}
