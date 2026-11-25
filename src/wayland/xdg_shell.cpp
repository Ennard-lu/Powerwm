// SPDX-License-Identifier: Apache-2.0

#include <wayland/display.hpp>
#include <wayland/xdg_shell.hpp>

#include <tuple>
#include <utility>
#include <xdg_shell_bindings.hpp>

namespace pwm::wayland {

namespace {

template <typename T>
auto discard_entry(std::vector<std::unique_ptr<T>>& objects, object_id id) -> bool {
    const auto iterator = std::ranges::find_if(
        objects, [id](const std::unique_ptr<T>& object) { return object->id == id; });
    if (iterator == objects.end()) {
        return false;
    }
    objects.erase(iterator);
    return true;
}

}

auto xdg_shell_protocol::surface_popup_geometry(object_id popup_surface) const
    -> std::optional<popup_geometry> {
    object_id role = 0;
    for (const auto& surface : surfaces_) {
        if (surface->surface == popup_surface && surface->role != 0) {
            role = surface->role;
            break;
        }
    }
    if (role == 0) {
        return std::nullopt;
    }
    const xdg_popup_object* popup = nullptr;
    for (const auto& candidate : popups_) {
        if (candidate->id == role) {
            popup = candidate.get();
            break;
        }
    }
    if (popup == nullptr || popup->parent == 0) {
        return std::nullopt;
    }
    for (const auto& surface : surfaces_) {
        if (surface->id == popup->parent) {
            pwm::wayland::popup_geometry result{
                .parent_surface = surface->surface,
                .bounds = {popup->x + surface->window_geometry.x,
                           popup->y + surface->window_geometry.y, popup->width, popup->height},
            };
            return result;
        }
    }
    return std::nullopt;
}

auto xdg_shell_protocol::dismiss_grabbed_popups(object_id hit_surface) -> void {
    for (const auto& popup : popups_) {
        if (!popup->grabbed) {
            continue;
        }
        bool inside = false;
        if (hit_surface != 0) {
            for (const auto& surface : surfaces_) {
                if (surface->surface == hit_surface && surface->role == popup->id) {
                    inside = true;
                    break;
                }
            }
        }
        if (!inside) {
            static_cast<void>(send_popup_done(popup->id));
            popup->grabbed = false;
        }
    }
}

auto xdg_shell_protocol::discard_object(object_id id) noexcept -> void {
    static_cast<void>(discard_entry(wm_bases_, id));
    static_cast<void>(discard_entry(positioners_, id));
    static_cast<void>(discard_entry(surfaces_, id));
    static_cast<void>(discard_entry(toplevels_, id));
    static_cast<void>(discard_entry(popups_, id));
}

auto xdg_positioner_object::request_destroy() -> result<void> {
    return shell->service().destroy_object(id);
}

auto xdg_positioner_object::request_set_size(i32 width, i32 height) -> result<void> {
    size = {0, 0, width, height};
    return {};
}

auto xdg_positioner_object::request_set_anchor_rect(i32 x, i32 y, i32 width, i32 height)
    -> result<void> {
    anchor_rect = {x, y, width, height};
    return {};
}

auto xdg_positioner_object::request_set_anchor(u32 value) -> result<void> {
    anchor = value;
    return {};
}

auto xdg_positioner_object::request_set_gravity(u32 value) -> result<void> {
    gravity = value;
    return {};
}

auto xdg_positioner_object::request_set_constraint_adjustment(u32 value) -> result<void> {
    constraint_adjustment = value;
    return {};
}

auto xdg_positioner_object::request_set_offset(i32 x, i32 y) -> result<void> {
    offset_x = x;
    offset_y = y;
    return {};
}

auto xdg_positioner_object::request_set_reactive() -> result<void> {
    return {};
}

auto xdg_positioner_object::request_set_parent_size(i32 width, i32 height) -> result<void> {
    parent_width = width;
    parent_height = height;
    return {};
}

auto xdg_positioner_object::request_set_parent_configure(u32 serial) -> result<void> {
    static_cast<void>(serial);
    return {};
}

auto xdg_toplevel_object::request_destroy() -> result<void> {
    return shell->service().destroy_object(id);
}

auto xdg_toplevel_object::request_set_parent(object_id value) -> result<void> {
    parent = value;
    return {};
}

auto xdg_toplevel_object::request_set_title(std::string_view value) -> result<void> {
    title = std::string{value};
    return {};
}

auto xdg_toplevel_object::request_set_app_id(std::string_view value) -> result<void> {
    app_id = std::string{value};
    return {};
}

auto xdg_toplevel_object::request_show_window_menu(object_id seat, u32 serial, i32 x, i32 y)
    -> result<void> {
    static_cast<void>(seat);
    static_cast<void>(serial);
    static_cast<void>(x);
    static_cast<void>(y);
    return {};
}

auto xdg_toplevel_object::request_move(object_id seat, u32 serial) -> result<void> {
    static_cast<void>(seat);
    static_cast<void>(serial);
    return {};
}

auto xdg_toplevel_object::request_resize(object_id seat, u32 serial, u32 edges) -> result<void> {
    static_cast<void>(seat);
    static_cast<void>(serial);
    static_cast<void>(edges);
    return {};
}

auto xdg_toplevel_object::request_set_max_size(i32 width, i32 height) -> result<void> {
    static_cast<void>(width);
    static_cast<void>(height);
    return {};
}

auto xdg_toplevel_object::request_set_min_size(i32 width, i32 height) -> result<void> {
    static_cast<void>(width);
    static_cast<void>(height);
    return {};
}

auto xdg_toplevel_object::request_set_maximized() -> result<void> {
    return {};
}

auto xdg_toplevel_object::request_unset_maximized() -> result<void> {
    return {};
}

auto xdg_toplevel_object::request_set_fullscreen(object_id output) -> result<void> {
    static_cast<void>(output);
    return {};
}

auto xdg_toplevel_object::request_unset_fullscreen() -> result<void> {
    return {};
}

auto xdg_toplevel_object::request_set_minimized() -> result<void> {
    return {};
}

auto xdg_popup_object::request_destroy() -> result<void> {
    return shell->service().destroy_object(id);
}

auto xdg_popup_object::request_grab(object_id seat, u32 serial) -> result<void> {
    static_cast<void>(seat);
    static_cast<void>(serial);
    grabbed = true;
    return {};
}

namespace {

auto compute_popup_position(const xdg_positioner_object& positioner, i32& x, i32& y, i32& width,
                            i32& height) -> void {
    constexpr u32 anchor_bottom = 2;
    constexpr u32 anchor_right = 8;
    width = positioner.size.width;
    height = positioner.size.height;
    x = positioner.anchor_rect.x + positioner.offset_x;
    y = positioner.anchor_rect.y + positioner.offset_y;
    if ((positioner.anchor & anchor_right) != 0) {
        x += positioner.anchor_rect.width;
    }
    if ((positioner.anchor & anchor_bottom) != 0) {
        y += positioner.anchor_rect.height;
    }
    if ((positioner.gravity & anchor_right) != 0) {
        x -= width;
    }
    if ((positioner.gravity & anchor_bottom) != 0) {
        y -= height;
    }
}

}

auto xdg_popup_object::request_reposition(object_id positioner, u32 token) -> result<void> {
    for (const auto& candidate : shell->positioners_) {
        if (candidate->id != positioner) {
            continue;
        }
        compute_popup_position(*candidate, x, y, width, height);
        if (auto configured = shell->send_popup_configure(id, x, y, width, height); !configured) {
            return configured;
        }
        return shell->send_repositioned(id, token);
    }
    return std::unexpected(
        make_error(error_code::protocol, "popup reposition references an unknown positioner"));
}

auto xdg_surface_object::request_destroy() -> result<void> {
    return shell->service().destroy_object(id);
}

auto xdg_surface_object::request_get_toplevel(object_id toplevel) -> result<void> {
    return shell->create_toplevel(toplevel, *this);
}

auto xdg_surface_object::request_get_popup(object_id popup, object_id parent, object_id positioner)
    -> result<void> {
    return shell->create_popup(popup, parent, positioner, *this);
}

auto xdg_surface_object::request_set_window_geometry(i32 x, i32 y, i32 width, i32 height)
    -> result<void> {
    window_geometry = {x, y, width, height};
    return {};
}

auto xdg_surface_object::request_ack_configure(u32 serial) -> result<void> {
    if (serial != configure_serial) {
        return std::unexpected(
            make_error(error_code::protocol, "ack_configure serial does not match"));
    }
    configured = true;
    return {};
}

auto xdg_wm_base_object::request_destroy() -> result<void> {
    return shell->service().destroy_object(id);
}

auto xdg_wm_base_object::request_create_positioner(object_id positioner) -> result<void> {
    return shell->create_positioner(positioner);
}

auto xdg_wm_base_object::request_get_xdg_surface(object_id xdg_surface, object_id surface)
    -> result<void> {
    return shell->create_surface(xdg_surface, surface);
}

auto xdg_wm_base_object::request_pong(u32 serial) -> result<void> {
    static_cast<void>(serial);
    return {};
}

xdg_shell_protocol::xdg_shell_protocol(display_service& service) noexcept : service_(&service) {
}

auto xdg_shell_protocol::bind_global(std::string_view interface, object_id id, u32 version)
    -> result<void> {
    static_cast<void>(version);
    if (interface != generated::xdg_wm_base::name) {
        return std::unexpected(
            make_error(error_code::unsupported, "xdg-shell interface is not supported"));
    }
    auto* object = store(wm_bases_, xdg_wm_base_object{this, id});
    return add_object<xdg_wm_base_object, generated::xdg_wm_base>(
        service_->connection_ref().objects(), id, *object);
}

auto xdg_shell_protocol::create_positioner(object_id id) -> result<void> {
    auto* object = store(positioners_, xdg_positioner_object{this, id});
    return add_object<xdg_positioner_object, generated::xdg_positioner>(
        service_->connection_ref().objects(), id, *object);
}

auto xdg_shell_protocol::create_surface(object_id id, object_id surface) -> result<void> {
    auto* object = store(surfaces_, xdg_surface_object{this, id, surface});
    return add_object<xdg_surface_object, generated::xdg_surface>(
        service_->connection_ref().objects(), id, *object);
}

auto xdg_shell_protocol::create_toplevel(object_id id, xdg_surface_object& surface)
    -> result<void> {
    auto* object = store(toplevels_, xdg_toplevel_object{this, id});
    if (auto added = add_object<xdg_toplevel_object, generated::xdg_toplevel>(
            service_->connection_ref().objects(), id, *object);
        !added) {
        return std::unexpected(added.error());
    }
    surface.role = id;
    surface.configure_serial = service_->next_serial();
    if (auto configured = send_toplevel_configure(id, 0, 0); !configured) {
        return std::unexpected(configured.error());
    }
    return send_surface_configure(surface.id, surface.configure_serial);
}

auto xdg_shell_protocol::create_popup(object_id id, object_id parent, object_id positioner,
                                      xdg_surface_object& surface) -> result<void> {
    auto* object = store(popups_, xdg_popup_object{this, id, parent, positioner});
    for (const auto& candidate : positioners_) {
        if (candidate->id == positioner) {
            compute_popup_position(*candidate, object->x, object->y, object->width, object->height);
            break;
        }
    }
    if (auto added = add_object<xdg_popup_object, generated::xdg_popup>(
            service_->connection_ref().objects(), id, *object);
        !added) {
        return std::unexpected(added.error());
    }
    surface.role = id;
    surface.configure_serial = service_->next_serial();
    if (auto configured =
            send_popup_configure(id, object->x, object->y, object->width, object->height);
        !configured) {
        return std::unexpected(configured.error());
    }
    return send_surface_configure(surface.id, surface.configure_serial);
}

auto xdg_shell_protocol::send_toplevel_configure(object_id id, i32 width, i32 height)
    -> result<void> {
    generated::xdg_toplevel::event_configure event{};
    std::get<0>(event.arguments) = width;
    std::get<1>(event.arguments) = height;

    protocol_writer writer;
    if (auto encoded = generated::xdg_toplevel::event_configure::encode(writer, id, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto xdg_shell_protocol::send_surface_configure(object_id id, u32 serial) -> result<void> {
    generated::xdg_surface::event_configure event{};
    std::get<0>(event.arguments) = serial;

    protocol_writer writer;
    if (auto encoded = generated::xdg_surface::event_configure::encode(writer, id, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto xdg_shell_protocol::send_popup_configure(object_id id, i32 x, i32 y, i32 width, i32 height)
    -> result<void> {
    generated::xdg_popup::event_configure event{};
    std::get<0>(event.arguments) = x;
    std::get<1>(event.arguments) = y;
    std::get<2>(event.arguments) = width;
    std::get<3>(event.arguments) = height;

    protocol_writer writer;
    if (auto encoded = generated::xdg_popup::event_configure::encode(writer, id, event); !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto xdg_shell_protocol::send_popup_done(object_id id) -> result<void> {
    protocol_writer writer;
    generated::xdg_popup::event_popup_done event{};
    if (auto encoded = generated::xdg_popup::event_popup_done::encode(writer, id, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto xdg_shell_protocol::send_repositioned(object_id id, u32 token) -> result<void> {
    generated::xdg_popup::event_repositioned event{};
    std::get<0>(event.arguments) = token;

    protocol_writer writer;
    if (auto encoded = generated::xdg_popup::event_repositioned::encode(writer, id, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto xdg_shell_protocol::service() noexcept -> display_service& {
    return *service_;
}

}
