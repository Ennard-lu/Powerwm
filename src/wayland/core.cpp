// SPDX-License-Identifier: Apache-2.0

#include <wayland/core.hpp>
#include <wayland/display.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fractional_scale_bindings.hpp>
#include <layer_shell_bindings.hpp>
#include <linux_dmabuf_bindings.hpp>
#include <presentation_time_bindings.hpp>
#include <primary_selection_bindings.hpp>
#include <string_view>
#include <tuple>
#include <utility>
#include <viewporter_bindings.hpp>
#include <wayland_bindings.hpp>
#include <xdg_activation_bindings.hpp>
#include <xdg_decoration_bindings.hpp>
#include <xdg_output_bindings.hpp>

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

auto valid_buffer_size(const shm_pool_object& pool, i32 offset, i32 width, i32 height, i32 stride)
    -> bool {
    if (width <= 0 || height <= 0 || offset < 0 ||
        static_cast<i64>(stride) < static_cast<i64>(width) * 4) {
        return false;
    }
    const i64 end = static_cast<i64>(offset) + static_cast<i64>(stride) * height;
    return end <= pool.size;
}

}

auto primary_source_object::request_offer(std::string_view mime) -> result<void> {
    mime_types.emplace_back(mime);
    return {};
}

auto primary_source_object::request_destroy() -> result<void> {
    core->service().notify_primary_source_destroyed(id);
    core->service().connection_ref().objects().remove(id);
    core->service().queue_discard(id);
    return {};
}

auto primary_offer_object::request_receive(std::string_view mime, handle_t fd) -> result<void> {
    if (std::find(mime_types.begin(), mime_types.end(), mime) == mime_types.end()) {
        return std::unexpected(
            make_error(error_code::protocol, "primary offer does not provide the mime type"));
    }
    auto owned = platform::wayland::file_descriptor::adopt(fd);
    return core->service().receive_primary_offer(id, source_client, source_object, mime,
                                                 owned.fd());
}

auto primary_offer_object::request_destroy() -> result<void> {
    core->service().connection_ref().objects().remove(id);
    core->service().queue_discard(id);
    return {};
}

auto primary_device_object::request_set_selection(object_id source, u32 serial) -> result<void> {
    selection = source;
    return core->service().notify_primary_selection(source, serial);
}

auto primary_device_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto primary_manager_object::request_create_source(object_id source) -> result<void> {
    auto* object = core->store(core->primary_sources_, primary_source_object{core, source});
    return add_object<primary_source_object, generated::zwp_primary_selection_source_v1>(
        core->service().connection_ref().objects(), source, *object);
}

auto primary_manager_object::request_get_device(object_id device, object_id seat) -> result<void> {
    static_cast<void>(seat);
    auto* object = core->store(core->primary_devices_, primary_device_object{core, device});
    return add_object<primary_device_object, generated::zwp_primary_selection_device_v1>(
        core->service().connection_ref().objects(), device, *object);
}

auto primary_manager_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto linux_buffer_params_object::request_destroy() -> result<void> {
    fd.destroy();
    return core->service().destroy_object(id);
}

auto linux_buffer_params_object::request_add(handle_t fd, u32 plane, u32 offset, u32 stride,
                                             u32 modifier_hi, u32 modifier_lo) -> result<void> {
    if (plane != 0 || this->fd.valid()) {
        return std::unexpected(
            make_error(error_code::protocol, "only a single DMA-BUF plane is supported"));
    }
    if (offset > stride) {
        return std::unexpected(make_error(error_code::protocol, "DMA-BUF offset is invalid"));
    }
    this->fd = platform::wayland::file_descriptor::adopt(fd);
    this->offset = offset;
    this->stride = stride;
    modifier = (static_cast<u64>(modifier_hi) << 32U) | static_cast<u64>(modifier_lo);
    plane_count = 1;
    return {};
}

auto linux_buffer_params_object::request_create(i32 width, i32 height, u32 format, u32 flags)
    -> result<void> {
    static_cast<void>(flags);
    if (!fd.valid() || width <= 0 || height <= 0) {
        return core->send_linux_buffer_failed(id);
    }
    const object_id buffer = core->allocate_server_object_id();
    auto created = core->create_dmabuf_buffer(buffer, std::move(fd), width, height, stride, format,
                                              modifier, true);
    if (!created) {
        return core->send_linux_buffer_failed(id);
    }
    return core->send_linux_buffer_created(id, buffer);
}

auto linux_buffer_params_object::request_create_immed(object_id buffer, i32 width, i32 height,
                                                      u32 format, u32 flags) -> result<void> {
    static_cast<void>(flags);
    if (!fd.valid() || width <= 0 || height <= 0) {
        return core->send_linux_buffer_failed(id);
    }
    return core->create_dmabuf_buffer(buffer, std::move(fd), width, height, stride, format,
                                      modifier, false);
}

auto linux_buffer_params_object::request_set_sampling_device(std::span<const std::byte> device)
    -> result<void> {
    static_cast<void>(device);
    return {};
}

auto linux_dmabuf_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto linux_dmabuf_object::request_create_params(object_id params) -> result<void> {
    return core->create_linux_buffer_params(params);
}

auto linux_dmabuf_object::request_get_default_feedback(object_id feedback) -> result<void> {
    static_cast<void>(feedback);
    return std::unexpected(
        make_error(error_code::unsupported, "DMA-BUF feedback is not supported"));
}

auto linux_dmabuf_object::request_get_surface_feedback(object_id feedback, object_id surface)
    -> result<void> {
    static_cast<void>(feedback);
    static_cast<void>(surface);
    return std::unexpected(
        make_error(error_code::unsupported, "DMA-BUF feedback is not supported"));
}

auto layer_surface_object::request_set_size(u32 width, u32 height) -> result<void> {
    this->width = width;
    this->height = height;
    return {};
}

auto layer_surface_object::request_set_anchor(u32 value) -> result<void> {
    if ((value & ~0xFU) != 0) {
        return std::unexpected(make_error(error_code::protocol, "layer anchor is invalid"));
    }
    anchor = value;
    return {};
}

auto layer_surface_object::request_set_exclusive_zone(i32 zone) -> result<void> {
    if (zone < -1) {
        return std::unexpected(make_error(error_code::protocol, "layer exclusive zone is invalid"));
    }
    exclusive = zone;
    return {};
}

auto layer_surface_object::request_set_margin(i32 top, i32 right, i32 bottom, i32 left)
    -> result<void> {
    margin_top = top;
    margin_right = right;
    margin_bottom = bottom;
    margin_left = left;
    return {};
}

auto layer_surface_object::request_set_keyboard_interactivity(u32 value) -> result<void> {
    if (value > 2) {
        return std::unexpected(
            make_error(error_code::protocol, "layer keyboard interactivity is invalid"));
    }
    keyboard = value;
    return {};
}

auto layer_surface_object::request_get_popup(object_id popup) -> result<void> {
    static_cast<void>(popup);
    return {};
}

auto layer_surface_object::request_ack_configure(u32 serial) -> result<void> {
    if (!configured || serial != configure_serial) {
        return std::unexpected(
            make_error(error_code::protocol, "layer configure serial is unknown"));
    }
    return {};
}

auto layer_surface_object::request_destroy() -> result<void> {
    surface = 0;
    return core->service().destroy_object(id);
}

auto layer_surface_object::request_set_layer(u32 value) -> result<void> {
    if (value > 3) {
        return std::unexpected(make_error(error_code::protocol, "layer value is invalid"));
    }
    layer = value;
    return {};
}

auto layer_shell_object::request_get_layer_surface(object_id layer_surface, object_id surface,
                                                   object_id output, u32 layer,
                                                   std::string_view space) -> result<void> {
    if (layer > 3) {
        return std::unexpected(make_error(error_code::protocol, "layer value is invalid"));
    }
    return core->create_layer_surface(layer_surface, surface, output, layer, space);
}

auto layer_shell_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto output_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto pointer_object::request_set_cursor(u32 serial, object_id surface, i32 hotspot_x, i32 hotspot_y)
    -> result<void> {
    static_cast<void>(serial);
    static_cast<void>(surface);
    static_cast<void>(hotspot_x);
    static_cast<void>(hotspot_y);
    return {};
}

auto pointer_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto keyboard_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto touch_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto seat_object::request_get_pointer(object_id pointer) -> result<void> {
    auto* object = core->store(core->pointers_, pointer_object{core, pointer});
    auto added = add_object<pointer_object, generated::wl_pointer>(
        core->service().connection_ref().objects(), pointer, *object);
    if (added) {
        core->pointer_ids_.push_back(pointer);
    }
    return added;
}

auto seat_object::request_get_keyboard(object_id keyboard) -> result<void> {
    auto* object = core->store(core->keyboards_, keyboard_object{core, keyboard});
    auto added = add_object<keyboard_object, generated::wl_keyboard>(
        core->service().connection_ref().objects(), keyboard, *object);
    if (added) {
        core->keyboard_ids_.push_back(keyboard);
        return core->send_keyboard_keymap(keyboard);
    }
    return added;
}

auto seat_object::request_get_touch(object_id touch) -> result<void> {
    auto* object = core->store(core->touches_, touch_object{core, touch});
    auto added = add_object<touch_object, generated::wl_touch>(
        core->service().connection_ref().objects(), touch, *object);
    if (added) {
        core->touch_ids_.push_back(touch);
    }
    return added;
}

auto seat_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto presentation_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto presentation_object::request_feedback(object_id feedback, object_id surface) -> result<void> {
    return core->service().request_presentation_feedback(feedback, surface);
}

auto activation_token_object::request_set_serial(u32 value, object_id seat) -> result<void> {
    static_cast<void>(seat);
    serial = value;
    return {};
}

auto activation_token_object::request_set_app_id(std::string_view value) -> result<void> {
    app_id = std::string{value};
    return {};
}

auto activation_token_object::request_set_surface(object_id surface) -> result<void> {
    this->surface = surface;
    return {};
}

auto activation_token_object::request_commit() -> result<void> {
    const std::string token = "powerwm-token-" + std::to_string(serial);
    generated::xdg_activation_token_v1::event_done event{};
    std::get<0>(event.arguments) = token;
    protocol_writer writer;
    if (auto encoded = generated::xdg_activation_token_v1::event_done::encode(writer, id, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return core->service().queue_event(writer);
}

auto activation_token_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto activation_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto activation_object::request_get_activation_token(object_id token) -> result<void> {
    auto* object = core->store(core->activation_tokens_, activation_token_object{core, token});
    return add_object<activation_token_object, generated::xdg_activation_token_v1>(
        core->service().connection_ref().objects(), token, *object);
}

auto activation_object::request_activate(std::string_view token, object_id surface)
    -> result<void> {
    static_cast<void>(token);
    static_cast<void>(surface);
    return {};
}

auto viewport_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto viewport_object::request_set_source(f64 x, f64 y, f64 width, f64 height) -> result<void> {
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(width);
    static_cast<void>(height);
    return {};
}

auto viewport_object::request_set_destination(i32 width, i32 height) -> result<void> {
    if (width < 0 || height < 0) {
        return std::unexpected(make_error(error_code::protocol, "viewport destination is invalid"));
    }
    destination_width = width;
    destination_height = height;
    return {};
}

auto viewporter_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto viewporter_object::request_get_viewport(object_id viewport, object_id surface)
    -> result<void> {
    auto* object = core->store(core->viewports_, viewport_object{core, viewport, surface});
    return add_object<viewport_object, generated::wp_viewport>(
        core->service().connection_ref().objects(), viewport, *object);
}

auto fractional_scale_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto fractional_scale_manager_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto fractional_scale_manager_object::request_get_fractional_scale(object_id fractional_scale,
                                                                   object_id surface)
    -> result<void> {
    static_cast<void>(surface);
    auto* object =
        core->store(core->fractional_scales_, fractional_scale_object{core, fractional_scale});
    if (auto added = add_object<fractional_scale_object, generated::wp_fractional_scale_v1>(
            core->service().connection_ref().objects(), fractional_scale, *object);
        !added) {
        return std::unexpected(added.error());
    }
    generated::wp_fractional_scale_v1::event_preferred_scale event{};
    std::get<0>(event.arguments) = 120;
    protocol_writer writer;
    if (auto encoded = generated::wp_fractional_scale_v1::event_preferred_scale::encode(
            writer, fractional_scale, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return core->service().queue_event(writer);
}

auto toplevel_decoration_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto toplevel_decoration_object::request_set_mode(u32 value) -> result<void> {
    mode = value;
    return {};
}

auto toplevel_decoration_object::request_unset_mode() -> result<void> {
    mode = 1;
    return {};
}

auto decoration_manager_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto decoration_manager_object::request_get_toplevel_decoration(object_id decoration,
                                                                object_id toplevel)
    -> result<void> {
    auto* object = core->store(core->toplevel_decorations_,
                               toplevel_decoration_object{core, decoration, toplevel});
    if (auto added = add_object<toplevel_decoration_object, generated::zxdg_toplevel_decoration_v1>(
            core->service().connection_ref().objects(), decoration, *object);
        !added) {
        return std::unexpected(added.error());
    }
    generated::zxdg_toplevel_decoration_v1::event_configure event{};
    std::get<0>(event.arguments) = 1;
    protocol_writer writer;
    if (auto encoded = generated::zxdg_toplevel_decoration_v1::event_configure::encode(
            writer, decoration, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return core->service().queue_event(writer);
}

auto xdg_output_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto xdg_output_manager_object::request_get_xdg_output(object_id xdg_output, object_id output)
    -> result<void> {
    const output_descriptor descriptor =
        core->output_descriptor_for(output).value_or(output_descriptor{});
    auto* object = core->store(core->xdg_outputs_, xdg_output_object{core, xdg_output, version});
    if (auto added = add_object<xdg_output_object, generated::zxdg_output_v1>(
            core->service().connection_ref().objects(), xdg_output, *object);
        !added) {
        return std::unexpected(added.error());
    }

    generated::zxdg_output_v1::event_logical_position position{};
    std::get<0>(position.arguments) = descriptor.x;
    std::get<1>(position.arguments) = descriptor.y;
    protocol_writer position_writer;
    if (auto encoded = generated::zxdg_output_v1::event_logical_position::encode(
            position_writer, xdg_output, position);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = core->service().queue_event(position_writer); !queued) {
        return std::unexpected(queued.error());
    }

    generated::zxdg_output_v1::event_logical_size size{};
    std::get<0>(size.arguments) = static_cast<i32>(descriptor.width);
    std::get<1>(size.arguments) = static_cast<i32>(descriptor.height);
    protocol_writer size_writer;
    if (auto encoded =
            generated::zxdg_output_v1::event_logical_size::encode(size_writer, xdg_output, size);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = core->service().queue_event(size_writer); !queued) {
        return std::unexpected(queued.error());
    }

    if (version >= 2) {
        generated::zxdg_output_v1::event_name name{};
        std::get<0>(name.arguments) = descriptor.name;
        protocol_writer name_writer;
        if (auto encoded =
                generated::zxdg_output_v1::event_name::encode(name_writer, xdg_output, name);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = core->service().queue_event(name_writer); !queued) {
            return std::unexpected(queued.error());
        }
        generated::zxdg_output_v1::event_description description{};
        std::get<0>(description.arguments) = descriptor.description;
        protocol_writer description_writer;
        if (auto encoded = generated::zxdg_output_v1::event_description::encode(
                description_writer, xdg_output, description);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = core->service().queue_event(description_writer); !queued) {
            return std::unexpected(queued.error());
        }
        generated::zxdg_output_v1::event_done done{};
        protocol_writer done_writer;
        if (auto encoded =
                generated::zxdg_output_v1::event_done::encode(done_writer, xdg_output, done);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        return core->service().queue_event(done_writer);
    }
    return {};
}

auto xdg_output_manager_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto subsurface_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto subsurface_object::request_set_position(i32 new_x, i32 new_y) -> result<void> {
    x = new_x;
    y = new_y;
    return {};
}

auto subsurface_object::request_place_above(object_id sibling) -> result<void> {
    static_cast<void>(sibling);
    return {};
}

auto subsurface_object::request_place_below(object_id sibling) -> result<void> {
    static_cast<void>(sibling);
    return {};
}

auto subsurface_object::request_set_sync() -> result<void> {
    return {};
}

auto subsurface_object::request_set_desync() -> result<void> {
    return {};
}

auto subcompositor_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto subcompositor_object::request_get_subsurface(object_id subsurface, object_id surface,
                                                  object_id parent) -> result<void> {
    auto* object =
        core->store(core->subsurfaces_, subsurface_object{core, subsurface, surface, parent});
    return add_object<subsurface_object, generated::wl_subsurface>(
        core->service().connection_ref().objects(), subsurface, *object);
}

auto data_offer_object::request_accept(u32 serial, std::string_view mime) -> result<void> {
    if (mime.empty()) {
        return {};
    }
    if (std::find(mime_types.begin(), mime_types.end(), mime) == mime_types.end()) {
        return std::unexpected(
            make_error(error_code::protocol, "data offer does not provide the mime type"));
    }
    return core->service().accept_data_offer(id, source_client, source_object, serial, mime);
}

auto data_offer_object::request_receive(std::string_view mime, handle_t fd) -> result<void> {
    if (std::find(mime_types.begin(), mime_types.end(), mime) == mime_types.end()) {
        return std::unexpected(
            make_error(error_code::protocol, "data offer does not provide the mime type"));
    }
    auto owned = platform::wayland::file_descriptor::adopt(fd);
    return core->service().receive_data_offer(id, source_client, source_object, mime, owned.fd());
}

auto data_offer_object::request_destroy() -> result<void> {
    core->service().connection_ref().objects().remove(id);
    core->service().queue_discard(id);
    return {};
}

auto data_offer_object::request_finish() -> result<void> {
    return core->service().finish_data_offer(id, source_client, source_object);
}

auto data_offer_object::request_set_actions(u32 source_actions, u32 action) -> result<void> {
    constexpr u32 action_mask = 0x7;
    if ((source_actions & ~action_mask) != 0 || (action & ~action_mask) != 0 ||
        (action & (action - 1)) != 0) {
        return std::unexpected(make_error(error_code::protocol, "drag action mask is invalid"));
    }
    if (action != 0 && (action & source_actions) == 0) {
        return std::unexpected(
            make_error(error_code::protocol, "drag action is not offered by the source"));
    }
    return core->service().data_offer_actions(id, source_client, source_object, source_actions,
                                              action);
}

auto data_source_object::request_offer(std::string_view mime) -> result<void> {
    mime_types.emplace_back(mime);
    return {};
}

auto data_source_object::request_destroy() -> result<void> {
    core->service().notify_data_source_destroyed(id);
    core->service().connection_ref().objects().remove(id);
    core->service().queue_discard(id);
    return {};
}

auto data_source_object::request_set_actions(u32 value) -> result<void> {
    actions = value;
    return {};
}

auto data_device_object::request_start_drag(object_id source, object_id origin, object_id icon,
                                            u32 serial) -> result<void> {
    if (source == 0) {
        return {};
    }
    return core->service().start_data_drag(source, origin, icon, serial);
}

auto data_device_object::request_set_selection(object_id source, u32 serial) -> result<void> {
    selection = source;
    return core->service().notify_data_source(source, serial);
}

auto data_device_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto data_device_manager_object::request_create_data_source(object_id source) -> result<void> {
    auto* object = core->store(core->data_sources_, data_source_object{core, source});
    return add_object<data_source_object, generated::wl_data_source>(
        core->service().connection_ref().objects(), source, *object);
}

auto data_device_manager_object::request_get_data_device(object_id device, object_id seat)
    -> result<void> {
    auto* object = core->store(core->data_devices_, data_device_object{core, device, seat});
    return add_object<data_device_object, generated::wl_data_device>(
        core->service().connection_ref().objects(), device, *object);
}

auto data_device_manager_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto compositor_object::request_create_surface(object_id surface) -> result<void> {
    return core->create_surface(surface);
}

auto compositor_object::request_create_region(object_id region) -> result<void> {
    return core->create_region(region);
}

auto compositor_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto region_object::request_add(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    if (width > 0 && height > 0) {
        rectangles.push_back({x, y, width, height});
    }
    return {};
}

auto region_object::request_subtract(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    const base::recti target{x, y, width, height};
    std::erase(rectangles, target);
    return {};
}

auto region_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto buffer_object::request_destroy() -> result<void> {
    if (server_created) {
        core->service().connection_ref().objects().remove(id);
        core->service().queue_discard(id);
        return {};
    }
    return core->service().destroy_object(id);
}

auto shm_pool_object::request_create_buffer(object_id buffer, i32 offset, i32 width, i32 height,
                                            i32 stride, u32 format) -> result<void> {
    if (!valid_buffer_size(*this, offset, width, height, stride)) {
        return std::unexpected(make_error(error_code::protocol, "shm buffer is out of bounds"));
    }
    return core->create_buffer(buffer, id, offset, width, height, stride, format);
}

auto shm_pool_object::request_destroy() -> result<void> {
    fd.destroy();
    return core->service().destroy_object(id);
}

auto shm_pool_object::request_resize(i32 size) -> result<void> {
    if (size <= 0) {
        return std::unexpected(make_error(error_code::protocol, "shm pool size is invalid"));
    }
    auto resized = platform::wayland::mapped_region::create(fd.fd(), 0, size);
    if (!resized) {
        return std::unexpected(resized.error());
    }
    mapping = std::move(*resized);
    this->size = size;
    return {};
}

auto shm_object::request_create_pool(object_id pool, handle_t fd, i32 size) -> result<void> {
    return core->create_pool(pool, fd, size);
}

auto shm_object::request_release() -> result<void> {
    return core->service().destroy_object(id);
}

auto surface_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto surface_object::request_attach(object_id new_buffer, i32 x, i32 y) -> result<void> {
    pending_buffer = new_buffer;
    pending_offset_x = x;
    pending_offset_y = y;
    buffer_pending = true;
    return {};
}

auto surface_object::request_damage(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    if (width > 0 && height > 0) {
        pending_damage.push_back({x, y, width, height});
    }
    return {};
}

auto surface_object::request_frame(object_id callback) -> result<void> {
    if (callback == 0) {
        return std::unexpected(make_error(error_code::protocol, "frame callback id is zero"));
    }
    pending_frame_callbacks.push_back(callback);
    return {};
}

auto surface_object::request_set_opaque_region(object_id region) -> result<void> {
    pending_opaque_region = region;
    return {};
}

auto surface_object::request_set_input_region(object_id region) -> result<void> {
    pending_input_region = region;
    return {};
}

auto surface_object::request_commit() -> result<void> {
    if (auto configured = core->configure_layer_surface(id); !configured) {
        return std::unexpected(configured.error());
    }
    if (buffer_pending) {
        buffer = pending_buffer;
        buffer_pending = false;
    }
    offset_x = pending_offset_x;
    offset_y = pending_offset_y;
    buffer_scale = pending_buffer_scale;
    buffer_transform = pending_buffer_transform;
    opaque_region = pending_opaque_region;
    input_region = pending_input_region;
    surface_commit_event event{.surface = id, .buffer = buffer};
    if (buffer != 0) {
        const buffer_object* committed = core->buffer(buffer);
        if (committed == nullptr) {
            return std::unexpected(make_error(error_code::protocol, "surface buffer is unknown"));
        }
        std::span<const std::byte> committed_pixels;
        if (!committed->is_dmabuf) {
            auto pixels = core->buffer_pixels(buffer);
            if (!pixels) {
                return std::unexpected(pixels.error());
            }
            committed_pixels = *pixels;
        }
        event.width = committed->width;
        event.height = committed->height;
        event.stride = committed->stride;
        event.format = committed->format;
        event.pixels = committed_pixels;
        if (committed->is_dmabuf) {
            event.is_dmabuf = true;
            event.fd = committed->dmabuf_fd.fd();
            event.modifier = committed->modifier;
        }
    }
    if (current_buffer != 0 && current_buffer != buffer) {
        if (auto released = core->send_buffer_release(current_buffer); !released) {
            return std::unexpected(released.error());
        }
    }
    current_buffer = buffer;
    core->notify_commit(event);
    if ((buffer != 0) != mapped_outputs) {
        mapped_outputs = buffer != 0;
        if (auto sent = core->send_surface_output_state(id, mapped_outputs); !sent) {
            return std::unexpected(sent.error());
        }
    }

    for (const object_id callback : pending_frame_callbacks) {
        auto deferred = core->queue_frame_callback(callback);
        if (!deferred) {
            return std::unexpected(deferred.error());
        }
        if (*deferred) {
            continue;
        }
        if (auto sent = core->service().send_callback_done(callback, core->service().next_serial());
            !sent) {
            return std::unexpected(sent.error());
        }
    }
    pending_frame_callbacks.clear();
    pending_damage.clear();
    return {};
}

auto surface_object::request_set_buffer_transform(i32 transform) -> result<void> {
    pending_buffer_transform = transform;
    return {};
}

auto surface_object::request_set_buffer_scale(i32 scale) -> result<void> {
    if (scale <= 0) {
        return std::unexpected(make_error(error_code::protocol, "buffer scale is invalid"));
    }
    pending_buffer_scale = scale;
    return {};
}

auto surface_object::request_damage_buffer(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    return request_damage(x, y, width, height);
}

auto surface_object::request_offset(i32 x, i32 y) -> result<void> {
    pending_offset_x = x;
    pending_offset_y = y;
    return {};
}

auto surface_object::request_get_release(object_id callback) -> result<void> {
    return core->service().send_callback_done(callback, core->service().next_serial());
}

core_protocol::core_protocol(display_service& service) noexcept : service_(&service) {
}

auto core_protocol::bind_global(std::string_view interface, object_id id, u32 version)
    -> result<void> {
    static_cast<void>(version);
    if (interface == generated::wp_presentation::name) {
        auto* object = store(presentations_, presentation_object{this, id});
        if (auto added = add_object<presentation_object, generated::wp_presentation>(
                service_->connection_ref().objects(), id, *object);
            !added) {
            return std::unexpected(added.error());
        }
        generated::wp_presentation::event_clock_id event{};
        std::get<0>(event.arguments) = 0;
        protocol_writer writer;
        if (auto encoded = generated::wp_presentation::event_clock_id::encode(writer, id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        return service_->queue_event(writer);
    }
    if (interface == generated::xdg_activation_v1::name) {
        auto* object = store(activations_, activation_object{this, id});
        return add_object<activation_object, generated::xdg_activation_v1>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::wp_viewporter::name) {
        auto* object = store(viewporters_, viewporter_object{this, id});
        return add_object<viewporter_object, generated::wp_viewporter>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::wp_fractional_scale_manager_v1::name) {
        auto* object = store(fractional_scale_managers_, fractional_scale_manager_object{this, id});
        return add_object<fractional_scale_manager_object,
                          generated::wp_fractional_scale_manager_v1>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::zxdg_decoration_manager_v1::name) {
        auto* object = store(decoration_managers_, decoration_manager_object{this, id});
        return add_object<decoration_manager_object, generated::zxdg_decoration_manager_v1>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::zxdg_output_manager_v1::name) {
        auto* object = store(xdg_output_managers_, xdg_output_manager_object{this, id, version});
        return add_object<xdg_output_manager_object, generated::zxdg_output_manager_v1>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::wl_subcompositor::name) {
        auto* object = store(subcompositors_, subcompositor_object{this, id});
        return add_object<subcompositor_object, generated::wl_subcompositor>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::zwp_primary_selection_device_manager_v1::name) {
        auto* object = store(primary_managers_, primary_manager_object{this, id});
        return add_object<primary_manager_object,
                          generated::zwp_primary_selection_device_manager_v1>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::zwlr_layer_shell_v1::name) {
        return create_layer_shell(id, version);
    }
    if (interface == generated::zwp_linux_dmabuf_v1::name) {
        auto* object = store(linux_dmabufs_, linux_dmabuf_object{this, id, version});
        if (auto added = add_object<linux_dmabuf_object, generated::zwp_linux_dmabuf_v1>(
                service_->connection_ref().objects(), id, *object);
            !added) {
            return std::unexpected(added.error());
        }
        constexpr u32 argb8888 = 0x34325241;
        constexpr u32 xrgb8888 = 0x34325258;
        for (const u32 format : {argb8888, xrgb8888}) {
            generated::zwp_linux_dmabuf_v1::event_format event{};
            std::get<0>(event.arguments) = format;
            protocol_writer writer;
            if (auto encoded =
                    generated::zwp_linux_dmabuf_v1::event_format::encode(writer, id, event);
                !encoded) {
                return std::unexpected(encoded.error());
            }
            if (auto queued = service_->queue_event(writer); !queued) {
                return std::unexpected(queued.error());
            }
        }
        if (version >= 3) {
            generated::zwp_linux_dmabuf_v1::event_modifier event{};
            std::get<0>(event.arguments) = 0;
            std::get<1>(event.arguments) = 0;
            std::get<2>(event.arguments) = 0;
            protocol_writer writer;
            if (auto encoded =
                    generated::zwp_linux_dmabuf_v1::event_modifier::encode(writer, id, event);
                !encoded) {
                return std::unexpected(encoded.error());
            }
            return service_->queue_event(writer);
        }
        return {};
    }
    if (interface == generated::wl_data_device_manager::name) {
        auto* object = store(data_managers_, data_device_manager_object{this, id});
        return add_object<data_device_manager_object, generated::wl_data_device_manager>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::wl_output::name) {
        return create_output(id, version);
    }
    if (interface == generated::wl_seat::name) {
        return create_seat(id, version);
    }
    if (interface == generated::wl_compositor::name) {
        auto* object = store(compositors_, compositor_object{this, id});
        return add_object<compositor_object, generated::wl_compositor>(
            service_->connection_ref().objects(), id, *object);
    }
    if (interface == generated::wl_shm::name) {
        auto* object = store(shms_, shm_object{this, id});
        if (auto added = add_object<shm_object, generated::wl_shm>(
                service_->connection_ref().objects(), id, *object);
            !added) {
            return std::unexpected(added.error());
        }
        return send_shm_formats(id);
    }
    return std::unexpected(
        make_error(error_code::unsupported, "global interface is not supported"));
}

auto core_protocol::configure_output(u32 width, u32 height, u32 refresh_hz) noexcept -> void {
    output_width_ = width;
    output_height_ = height;
    output_refresh_hz_ = refresh_hz;
}

auto core_protocol::configure_outputs(std::span<const output_descriptor> outputs) noexcept -> void {
    output_descriptors_.assign(outputs.begin(), outputs.end());
}

auto core_protocol::output_descriptor_for(object_id id) const -> std::optional<output_descriptor> {
    for (const auto& output : outputs_) {
        if (output->id != id) {
            continue;
        }
        if (output->descriptor_index < output_descriptors_.size()) {
            return output_descriptors_[output->descriptor_index];
        }
        output_descriptor fallback;
        fallback.width = output_width_;
        fallback.height = output_height_;
        fallback.refresh_hz = output_refresh_hz_;
        return fallback;
    }
    return std::nullopt;
}

auto core_protocol::create_output(object_id id, u32 version) -> result<void> {
    auto* object = store(outputs_, output_object{this, id, version, next_output_index_++});
    if (auto added = add_object<output_object, generated::wl_output>(
            service_->connection_ref().objects(), id, *object);
        !added) {
        return std::unexpected(added.error());
    }
    return send_output_events(id, version);
}

auto core_protocol::create_seat(object_id id, u32 version) -> result<void> {
    auto* object = store(seats_, seat_object{this, id, version});
    if (auto added = add_object<seat_object, generated::wl_seat>(
            service_->connection_ref().objects(), id, *object);
        !added) {
        return std::unexpected(added.error());
    }
    return send_seat_events(id, version);
}

auto core_protocol::create_surface(object_id id) -> result<void> {
    auto* object = store(surfaces_, surface_object{this, id});
    return add_object<surface_object, generated::wl_surface>(service_->connection_ref().objects(),
                                                             id, *object);
}

auto core_protocol::create_region(object_id id) -> result<void> {
    auto* object = store(regions_, region_object{this, id});
    return add_object<region_object, generated::wl_region>(service_->connection_ref().objects(), id,
                                                           *object);
}

auto core_protocol::create_pool(object_id id, handle_t fd, i32 size) -> result<void> {
    if (size <= 0) {
        return std::unexpected(make_error(error_code::protocol, "shm pool size is invalid"));
    }
    auto owned = platform::wayland::file_descriptor::adopt(fd);
    auto mapping = platform::wayland::mapped_region::create(owned.fd(), 0, size);
    if (!mapping) {
        return std::unexpected(mapping.error());
    }
    auto* object =
        store(pools_, shm_pool_object{this, id, std::move(owned), size, std::move(*mapping)});
    return add_object<shm_pool_object, generated::wl_shm_pool>(service_->connection_ref().objects(),
                                                               id, *object);
}

auto core_protocol::send_surface_output_state(object_id surface, bool enter) -> result<void> {
    for (const auto& output : outputs_) {
        protocol_writer writer;
        if (enter) {
            generated::wl_surface::event_enter event{};
            std::get<0>(event.arguments) = output->id;
            if (auto encoded = generated::wl_surface::event_enter::encode(writer, surface, event);
                !encoded) {
                return std::unexpected(encoded.error());
            }
        } else {
            generated::wl_surface::event_leave event{};
            std::get<0>(event.arguments) = output->id;
            if (auto encoded = generated::wl_surface::event_leave::encode(writer, surface, event);
                !encoded) {
                return std::unexpected(encoded.error());
            }
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_buffer_release(object_id buffer) -> result<void> {
    if (buffer == 0 || this->buffer(buffer) == nullptr) {
        return {};
    }
    generated::wl_buffer::event_release event{};
    protocol_writer writer;
    if (auto encoded = generated::wl_buffer::event_release::encode(writer, buffer, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::discard_object(object_id id) noexcept -> void {
    static_cast<void>(discard_entry(compositors_, id));
    static_cast<void>(discard_entry(regions_, id));
    static_cast<void>(discard_entry(buffers_, id));
    static_cast<void>(discard_entry(pools_, id));
    static_cast<void>(discard_entry(shms_, id));
    static_cast<void>(discard_entry(surfaces_, id));
    static_cast<void>(discard_entry(outputs_, id));
    static_cast<void>(discard_entry(seats_, id));
    static_cast<void>(discard_entry(pointers_, id));
    static_cast<void>(discard_entry(keyboards_, id));
    static_cast<void>(discard_entry(touches_, id));
    static_cast<void>(discard_entry(presentations_, id));
    static_cast<void>(discard_entry(presentation_feedbacks_, id));
    static_cast<void>(discard_entry(activations_, id));
    static_cast<void>(discard_entry(activation_tokens_, id));
    static_cast<void>(discard_entry(viewporters_, id));
    static_cast<void>(discard_entry(viewports_, id));
    static_cast<void>(discard_entry(fractional_scale_managers_, id));
    static_cast<void>(discard_entry(fractional_scales_, id));
    static_cast<void>(discard_entry(decoration_managers_, id));
    static_cast<void>(discard_entry(toplevel_decorations_, id));
    static_cast<void>(discard_entry(xdg_output_managers_, id));
    static_cast<void>(discard_entry(xdg_outputs_, id));
    static_cast<void>(discard_entry(subcompositors_, id));
    static_cast<void>(discard_entry(subsurfaces_, id));
    static_cast<void>(discard_entry(data_managers_, id));
    static_cast<void>(discard_entry(data_devices_, id));
    static_cast<void>(discard_entry(data_sources_, id));
    static_cast<void>(discard_entry(data_offers_, id));
    static_cast<void>(discard_entry(linux_dmabufs_, id));
    static_cast<void>(discard_entry(linux_buffer_params_, id));
    static_cast<void>(discard_entry(layer_shells_, id));
    static_cast<void>(discard_entry(layer_surfaces_, id));
    static_cast<void>(discard_entry(primary_managers_, id));
    static_cast<void>(discard_entry(primary_devices_, id));
    static_cast<void>(discard_entry(primary_sources_, id));
    static_cast<void>(discard_entry(primary_offers_, id));
}

auto core_protocol::set_defer_frame_callbacks(bool value) noexcept -> void {
    defer_frame_callbacks_ = value;
}

auto core_protocol::queue_frame_callback(object_id callback) -> result<bool> {
    if (!defer_frame_callbacks_) {
        return false;
    }
    deferred_frame_callbacks_.push_back(callback);
    return true;
}

auto core_protocol::flush_frame_callbacks() -> result<void> {
    if (deferred_frame_callbacks_.empty()) {
        return {};
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
    for (const object_id callback : deferred_frame_callbacks_) {
        if (auto sent = service_->send_callback_done(callback, static_cast<u32>(now)); !sent) {
            return std::unexpected(sent.error());
        }
    }
    deferred_frame_callbacks_.clear();
    return {};
}

auto core_protocol::allocate_server_object_id() noexcept -> object_id {
    return next_server_id_++;
}

auto core_protocol::viewport_destination(object_id surface) const -> std::optional<base::recti> {
    for (const auto& viewport : viewports_) {
        if (viewport->surface == surface && viewport->destination_width > 0 &&
            viewport->destination_height > 0) {
            return base::recti{0, 0, viewport->destination_width, viewport->destination_height};
        }
    }
    return std::nullopt;
}

auto core_protocol::surface_buffer_scale(object_id surface) const noexcept -> i32 {
    for (const auto& entry : surfaces_) {
        if (entry->id == surface) {
            return entry->buffer_scale;
        }
    }
    return 1;
}

auto core_protocol::create_layer_shell(object_id id, u32 version) -> result<void> {
    auto* object = store(layer_shells_, layer_shell_object{this, id, version});
    return add_object<layer_shell_object, generated::zwlr_layer_shell_v1>(
        service_->connection_ref().objects(), id, *object);
}

auto core_protocol::create_layer_surface(object_id id, object_id surface, object_id output,
                                         u32 layer, std::string_view space) -> result<void> {
    if (surface == 0 || this->surface(surface) == nullptr) {
        return std::unexpected(make_error(error_code::protocol, "layer surface is unknown"));
    }
    for (const auto& existing : layer_surfaces_) {
        if (existing->surface == surface) {
            return std::unexpected(
                make_error(error_code::protocol, "surface already has a layer role"));
        }
    }
    auto* object = store(layer_surfaces_, layer_surface_object{.core = this,
                                                               .id = id,
                                                               .surface = surface,
                                                               .output = output,
                                                               .layer = layer,
                                                               .space = std::string{space}});
    return add_object<layer_surface_object, generated::zwlr_layer_surface_v1>(
        service_->connection_ref().objects(), id, *object);
}

auto core_protocol::layer_geometry(object_id surface, u32 output_width, u32 output_height,
                                   i32 buffer_width, i32 buffer_height) const
    -> std::optional<layer_surface_geometry> {
    constexpr u32 anchor_top = 1;
    constexpr u32 anchor_bottom = 2;
    constexpr u32 anchor_left = 4;
    constexpr u32 anchor_right = 8;
    for (const auto& layer : layer_surfaces_) {
        if (layer->surface != surface || layer->surface == 0) {
            continue;
        }
        const bool left = (layer->anchor & anchor_left) != 0;
        const bool right = (layer->anchor & anchor_right) != 0;
        const bool top = (layer->anchor & anchor_top) != 0;
        const bool bottom = (layer->anchor & anchor_bottom) != 0;
        i32 width = layer->width != 0 ? static_cast<i32>(layer->width) : buffer_width;
        i32 height = layer->height != 0 ? static_cast<i32>(layer->height) : buffer_height;
        if (layer->width == 0 && left && right) {
            width = static_cast<i32>(output_width) - layer->margin_left - layer->margin_right;
        }
        if (layer->height == 0 && top && bottom) {
            height = static_cast<i32>(output_height) - layer->margin_top - layer->margin_bottom;
        }
        width = std::max(1, width);
        height = std::max(1, height);
        i32 x = (static_cast<i32>(output_width) - width) / 2;
        i32 y = (static_cast<i32>(output_height) - height) / 2;
        if (left) {
            x = layer->margin_left;
        } else if (right) {
            x = static_cast<i32>(output_width) - width - layer->margin_right;
        }
        if (top) {
            y = layer->margin_top;
        } else if (bottom) {
            y = static_cast<i32>(output_height) - height - layer->margin_bottom;
        }
        return layer_surface_geometry{
            .bounds = {x, y, width, height},
            .layer = layer->layer,
            .exclusive = layer->exclusive,
            .keyboard_interactive = layer->keyboard != 0,
        };
    }
    return std::nullopt;
}

auto core_protocol::configure_layer_surface(object_id surface) -> result<void> {
    for (const auto& layer : layer_surfaces_) {
        if (layer->surface != surface || layer->configured) {
            continue;
        }
        generated::zwlr_layer_surface_v1::event_configure event{};
        const u32 serial = service_->next_serial();
        std::get<0>(event.arguments) = serial;
        std::get<1>(event.arguments) = layer->width;
        std::get<2>(event.arguments) = layer->height;
        protocol_writer writer;
        if (auto encoded =
                generated::zwlr_layer_surface_v1::event_configure::encode(writer, layer->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
        layer->configured = true;
        layer->configure_serial = serial;
        return {};
    }
    return {};
}

auto core_protocol::create_primary_offer(u32 source_client, object_id source_object,
                                         std::span<const std::string> mime_types)
    -> result<object_id> {
    const object_id offer = next_server_id_++;
    auto* object =
        store(primary_offers_, primary_offer_object{this, offer, source_client, source_object});
    object->mime_types.assign(mime_types.begin(), mime_types.end());
    if (auto added = add_object<primary_offer_object, generated::zwp_primary_selection_offer_v1>(
            service_->connection_ref().objects(), offer, *object, true);
        !added) {
        return std::unexpected(added.error());
    }
    for (const std::string& mime : object->mime_types) {
        generated::zwp_primary_selection_offer_v1::event_offer event{};
        std::get<0>(event.arguments) = mime;
        protocol_writer writer;
        if (auto encoded = generated::zwp_primary_selection_offer_v1::event_offer::encode(
                writer, offer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return offer;
}

auto core_protocol::primary_source_mime_types(object_id source) const
    -> std::span<const std::string> {
    for (const auto& object : primary_sources_) {
        if (object->id == source) {
            return object->mime_types;
        }
    }
    return {};
}

auto core_protocol::send_primary_selection(object_id offer) -> result<void> {
    for (const auto& device : primary_devices_) {
        generated::zwp_primary_selection_device_v1::event_selection event{};
        std::get<0>(event.arguments) = offer;
        protocol_writer writer;
        if (auto encoded = generated::zwp_primary_selection_device_v1::event_selection::encode(
                writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_primary_source_send(object_id source, std::string_view mime, handle_t fd)
    -> result<void> {
    bool found = false;
    for (const auto& object : primary_sources_) {
        if (object->id != source) {
            continue;
        }
        found = true;
        generated::zwp_primary_selection_source_v1::event_send event{};
        std::get<0>(event.arguments) = mime;
        std::get<1>(event.arguments) = fd;
        protocol_writer writer;
        if (auto encoded = generated::zwp_primary_selection_source_v1::event_send::encode(
                writer, source, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
        break;
    }
    if (!found) {
        return std::unexpected(make_error(error_code::protocol, "primary source no longer exists"));
    }
    return service_->connection_ref().flush();
}

auto core_protocol::send_primary_source_cancelled(object_id source) -> result<void> {
    generated::zwp_primary_selection_source_v1::event_cancelled event{};
    protocol_writer writer;
    if (auto encoded = generated::zwp_primary_selection_source_v1::event_cancelled::encode(
            writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::create_linux_buffer_params(object_id id) -> result<void> {
    auto* object = store(linux_buffer_params_, linux_buffer_params_object{this, id});
    return add_object<linux_buffer_params_object, generated::zwp_linux_buffer_params_v1>(
        service_->connection_ref().objects(), id, *object);
}

auto core_protocol::send_linux_buffer_created(object_id params, object_id buffer) -> result<void> {
    generated::zwp_linux_buffer_params_v1::event_created event{};
    std::get<0>(event.arguments) = buffer;
    protocol_writer writer;
    if (auto encoded =
            generated::zwp_linux_buffer_params_v1::event_created::encode(writer, params, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_linux_buffer_failed(object_id params) -> result<void> {
    generated::zwp_linux_buffer_params_v1::event_failed event{};
    protocol_writer writer;
    if (auto encoded =
            generated::zwp_linux_buffer_params_v1::event_failed::encode(writer, params, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::create_dmabuf_buffer(object_id id, platform::wayland::file_descriptor fd,
                                         i32 width, i32 height, u32 stride, u32 format,
                                         u64 modifier, bool server_created) -> result<void> {
    if (width <= 0 || height <= 0 || stride < static_cast<u32>(width) * 4) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "DMA-BUF dimensions are invalid"));
    }
    auto* object = store(
        buffers_, buffer_object{this, id, 0, 0, width, height, static_cast<i32>(stride), format});
    object->is_dmabuf = true;
    object->server_created = server_created;
    object->dmabuf_fd = std::move(fd);
    object->modifier = modifier;
    return add_object<buffer_object, generated::wl_buffer>(service_->connection_ref().objects(), id,
                                                           *object, server_created);
}

auto core_protocol::create_buffer(object_id id, object_id pool, i32 offset, i32 width, i32 height,
                                  i32 stride, u32 format) -> result<void> {
    auto* object =
        store(buffers_, buffer_object{this, id, pool, offset, width, height, stride, format});
    return add_object<buffer_object, generated::wl_buffer>(service_->connection_ref().objects(), id,
                                                           *object);
}

auto core_protocol::surface(object_id id) noexcept -> surface_object* {
    const auto iterator =
        std::ranges::find_if(surfaces_, [id](const std::unique_ptr<surface_object>& object) {
            return object->id == id;
        });
    return iterator == surfaces_.end() ? nullptr : iterator->get();
}

auto core_protocol::buffer(object_id id) noexcept -> buffer_object* {
    const auto iterator = std::ranges::find_if(
        buffers_, [id](const std::unique_ptr<buffer_object>& object) { return object->id == id; });
    return iterator == buffers_.end() ? nullptr : iterator->get();
}

auto core_protocol::buffer_pixels(object_id id) noexcept -> result<std::span<const std::byte>> {
    buffer_object* target = buffer(id);
    if (target == nullptr) {
        return std::unexpected(make_error(error_code::protocol, "buffer is unknown"));
    }
    const auto pool =
        std::ranges::find_if(pools_, [target](const std::unique_ptr<shm_pool_object>& object) {
            return object->id == target->pool;
        });
    if (pool == pools_.end()) {
        return std::unexpected(make_error(error_code::protocol, "buffer pool is unknown"));
    }
    const usize start = static_cast<usize>(target->offset);
    const usize length = static_cast<usize>(target->stride) * static_cast<usize>(target->height);
    const auto data = (*pool)->mapping.data();
    if (start > data.size() || length > data.size() - start) {
        return std::unexpected(make_error(error_code::protocol, "buffer is outside its pool"));
    }
    return data.subspan(start, length);
}

auto core_protocol::keyboards() const noexcept -> std::span<const object_id> {
    return keyboard_ids_;
}

auto core_protocol::pointers() const noexcept -> std::span<const object_id> {
    return pointer_ids_;
}

auto core_protocol::touches() const noexcept -> std::span<const object_id> {
    return touch_ids_;
}

auto core_protocol::create_data_offer(u32 source_client, object_id source_object,
                                      std::span<const std::string> mime_types, u32 actions)
    -> result<object_id> {
    const object_id offer = next_server_id_++;
    auto* object =
        store(data_offers_, data_offer_object{this, offer, source_client, source_object});
    object->mime_types.assign(mime_types.begin(), mime_types.end());
    if (auto added = add_object<data_offer_object, generated::wl_data_offer>(
            service_->connection_ref().objects(), offer, *object, true);
        !added) {
        return std::unexpected(added.error());
    }
    for (const std::string& mime : object->mime_types) {
        generated::wl_data_offer::event_offer event{};
        std::get<0>(event.arguments) = mime;
        protocol_writer writer;
        if (auto encoded = generated::wl_data_offer::event_offer::encode(writer, offer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    if (actions != 0) {
        generated::wl_data_offer::event_source_actions event{};
        std::get<0>(event.arguments) = actions;
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_offer::event_source_actions::encode(writer, offer, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return offer;
}

auto core_protocol::data_source_mime_types(object_id source) const -> std::span<const std::string> {
    for (const auto& object : data_sources_) {
        if (object->id == source) {
            return object->mime_types;
        }
    }
    return {};
}

auto core_protocol::data_source_actions(object_id source) const noexcept -> u32 {
    for (const auto& object : data_sources_) {
        if (object->id == source) {
            return object->actions;
        }
    }
    return 0;
}

auto core_protocol::send_selection(object_id offer) -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_selection event{};
        std::get<0>(event.arguments) = offer;
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_device::event_selection::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_data_source_send(object_id source, std::string_view mime, handle_t fd)
    -> result<void> {
    bool found = false;
    for (const auto& object : data_sources_) {
        if (object->id != source) {
            continue;
        }
        found = true;
        generated::wl_data_source::event_send event{};
        std::get<0>(event.arguments) = mime;
        std::get<1>(event.arguments) = fd;
        protocol_writer writer;
        if (auto encoded = generated::wl_data_source::event_send::encode(writer, source, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
        break;
    }
    if (!found) {
        return std::unexpected(make_error(error_code::protocol, "data source no longer exists"));
    }
    return service_->connection_ref().flush();
}

auto core_protocol::send_data_source_target(object_id source, std::string_view mime)
    -> result<void> {
    generated::wl_data_source::event_target event{};
    std::get<0>(event.arguments) = mime;
    protocol_writer writer;
    if (auto encoded = generated::wl_data_source::event_target::encode(writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_data_source_cancelled(object_id source) -> result<void> {
    generated::wl_data_source::event_cancelled event{};
    protocol_writer writer;
    if (auto encoded = generated::wl_data_source::event_cancelled::encode(writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_data_source_action(object_id source, u32 action) -> result<void> {
    generated::wl_data_source::event_action event{};
    std::get<0>(event.arguments) = action;
    protocol_writer writer;
    if (auto encoded = generated::wl_data_source::event_action::encode(writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_data_source_dnd_drop_performed(object_id source) -> result<void> {
    generated::wl_data_source::event_dnd_drop_performed event{};
    protocol_writer writer;
    if (auto encoded =
            generated::wl_data_source::event_dnd_drop_performed::encode(writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_data_source_dnd_finished(object_id source) -> result<void> {
    generated::wl_data_source::event_dnd_finished event{};
    protocol_writer writer;
    if (auto encoded = generated::wl_data_source::event_dnd_finished::encode(writer, source, event);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(writer);
}

auto core_protocol::send_data_offer_event(object_id offer) -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_data_offer event{};
        std::get<0>(event.arguments) = offer;
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_device::event_data_offer::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_data_device_enter(u32 serial, object_id surface, f64 x, f64 y,
                                           object_id offer) -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_enter event{};
        std::get<0>(event.arguments) = serial;
        std::get<1>(event.arguments) = surface;
        std::get<2>(event.arguments) = x;
        std::get<3>(event.arguments) = y;
        std::get<4>(event.arguments) = offer;
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_device::event_enter::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_data_device_leave() -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_leave event{};
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_device::event_leave::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_data_device_motion(u32 time_ms, f64 x, f64 y) -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_motion event{};
        std::get<0>(event.arguments) = time_ms;
        std::get<1>(event.arguments) = x;
        std::get<2>(event.arguments) = y;
        protocol_writer writer;
        if (auto encoded =
                generated::wl_data_device::event_motion::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::send_data_device_drop() -> result<void> {
    for (const auto& device : data_devices_) {
        generated::wl_data_device::event_drop event{};
        protocol_writer writer;
        if (auto encoded = generated::wl_data_device::event_drop::encode(writer, device->id, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto core_protocol::destroy_data_offer(object_id offer) -> void {
    service_->connection_ref().objects().remove(offer);
}

auto core_protocol::send_keyboard_keymap(object_id id) -> result<void> {
    constexpr std::string_view keymap = "xkb_keymap {\n"
                                        "  xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
                                        "  xkb_types { include \"complete\" };\n"
                                        "  xkb_compat { include \"complete\" };\n"
                                        "  xkb_symbols { include \"pc+us+inet(evdev)\" };\n"
                                        "  xkb_geometry { include \"pc(pc105)\" };\n"
                                        "};\n";
    auto file = platform::wayland::create_anonymous_file("powerwm-keymap", keymap.size() + 1);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto mapping = platform::wayland::mapped_region::create(file->fd(), 0, keymap.size() + 1, true);
    if (!mapping) {
        return std::unexpected(mapping.error());
    }
    auto writable = mapping->writable_data();
    std::memcpy(writable.data(), keymap.data(), keymap.size());
    writable[keymap.size()] = std::byte{0};

    generated::wl_keyboard::event_keymap event{};
    std::get<0>(event.arguments) = 1;
    std::get<1>(event.arguments) = file->fd();
    std::get<2>(event.arguments) = static_cast<u32>(keymap.size() + 1);

    protocol_writer writer;
    if (auto encoded = generated::wl_keyboard::event_keymap::encode(writer, id, event); !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = service_->queue_event(writer); !queued) {
        return std::unexpected(queued.error());
    }
    return service_->connection_ref().flush();
}

auto core_protocol::service() noexcept -> display_service& {
    return *service_;
}

auto core_protocol::set_surface_commit_callback(surface_commit_callback callback,
                                                void* context) noexcept -> void {
    commit_callback_ = callback;
    commit_context_ = context;
}

auto core_protocol::notify_commit(const surface_commit_event& event) noexcept -> void {
    if (commit_callback_ != nullptr) {
        commit_callback_(commit_context_, event);
    }
}

auto core_protocol::send_output_events(object_id id, u32 version) -> result<void> {
    const output_descriptor descriptor = output_descriptor_for(id).value_or(output_descriptor{});
    generated::wl_output::event_geometry geometry{};
    std::get<0>(geometry.arguments) = descriptor.x;
    std::get<1>(geometry.arguments) = descriptor.y;
    std::get<2>(geometry.arguments) = 0;
    std::get<3>(geometry.arguments) = 0;
    std::get<4>(geometry.arguments) = 0;
    std::get<5>(geometry.arguments) = std::string_view{"powerwm"};
    std::get<6>(geometry.arguments) = std::string_view{descriptor.name};
    std::get<7>(geometry.arguments) = 0;
    protocol_writer geometry_writer;
    if (auto encoded = generated::wl_output::event_geometry::encode(geometry_writer, id, geometry);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = service_->queue_event(geometry_writer); !queued) {
        return std::unexpected(queued.error());
    }

    generated::wl_output::event_mode mode{};
    std::get<0>(mode.arguments) = 3;
    std::get<1>(mode.arguments) = static_cast<i32>(descriptor.width);
    std::get<2>(mode.arguments) = static_cast<i32>(descriptor.height);
    std::get<3>(mode.arguments) = static_cast<i32>(descriptor.refresh_hz * 1000U);
    protocol_writer mode_writer;
    if (auto encoded = generated::wl_output::event_mode::encode(mode_writer, id, mode); !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = service_->queue_event(mode_writer); !queued) {
        return std::unexpected(queued.error());
    }

    if (version < 2) {
        return {};
    }
    generated::wl_output::event_scale scale{};
    std::get<0>(scale.arguments) = static_cast<i32>(descriptor.scale);
    protocol_writer scale_writer;
    if (auto encoded = generated::wl_output::event_scale::encode(scale_writer, id, scale);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = service_->queue_event(scale_writer); !queued) {
        return std::unexpected(queued.error());
    }

    if (version >= 4) {
        generated::wl_output::event_name name{};
        std::get<0>(name.arguments) = descriptor.name;
        protocol_writer name_writer;
        if (auto encoded = generated::wl_output::event_name::encode(name_writer, id, name);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(name_writer); !queued) {
            return std::unexpected(queued.error());
        }
        generated::wl_output::event_description description{};
        std::get<0>(description.arguments) = descriptor.description;
        protocol_writer description_writer;
        if (auto encoded = generated::wl_output::event_description::encode(description_writer, id,
                                                                           description);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(description_writer); !queued) {
            return std::unexpected(queued.error());
        }
    }

    generated::wl_output::event_done done{};
    protocol_writer done_writer;
    if (auto encoded = generated::wl_output::event_done::encode(done_writer, id, done); !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(done_writer);
}

auto core_protocol::send_seat_events(object_id id, u32 version) -> result<void> {
    generated::wl_seat::event_capabilities capabilities{};
    std::get<0>(capabilities.arguments) = 3;
    protocol_writer capabilities_writer;
    if (auto encoded =
            generated::wl_seat::event_capabilities::encode(capabilities_writer, id, capabilities);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = service_->queue_event(capabilities_writer); !queued) {
        return std::unexpected(queued.error());
    }
    if (version < 2) {
        return {};
    }
    generated::wl_seat::event_name name{};
    std::get<0>(name.arguments) = "seat0";
    protocol_writer name_writer;
    if (auto encoded = generated::wl_seat::event_name::encode(name_writer, id, name); !encoded) {
        return std::unexpected(encoded.error());
    }
    return service_->queue_event(name_writer);
}

auto core_protocol::send_shm_formats(object_id id) -> result<void> {
    for (const u32 format : {0U, 1U}) {
        generated::wl_shm::event_format event{};
        std::get<0>(event.arguments) = format;
        protocol_writer writer;
        if (auto encoded = generated::wl_shm::event_format::encode(writer, id, event); !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

}
