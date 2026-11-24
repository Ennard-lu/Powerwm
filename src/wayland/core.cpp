// SPDX-License-Identifier: Apache-2.0

#include <wayland/core.hpp>
#include <wayland/display.hpp>

#include <algorithm>
#include <cstring>
#include <string_view>
#include <tuple>
#include <utility>
#include <wayland_bindings.hpp>

namespace pwm::wayland {

namespace {

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
    static_cast<void>(serial);
    static_cast<void>(mime);
    return {};
}

auto data_offer_object::request_receive(std::string_view mime, handle_t fd) -> result<void> {
    static_cast<void>(mime);
    auto owned = platform::wayland::file_descriptor::adopt(fd);
    static_cast<void>(owned);
    return {};
}

auto data_offer_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto data_offer_object::request_finish() -> result<void> {
    return {};
}

auto data_offer_object::request_set_actions(u32 source_actions, u32 action) -> result<void> {
    static_cast<void>(source_actions);
    static_cast<void>(action);
    return {};
}

auto data_source_object::request_offer(std::string_view mime) -> result<void> {
    mime_types.emplace_back(mime);
    return {};
}

auto data_source_object::request_destroy() -> result<void> {
    return core->service().destroy_object(id);
}

auto data_source_object::request_set_actions(u32 value) -> result<void> {
    actions = value;
    return {};
}

auto data_device_object::request_start_drag(object_id source, object_id origin, object_id icon,
                                            u32 serial) -> result<void> {
    static_cast<void>(source);
    static_cast<void>(origin);
    static_cast<void>(icon);
    static_cast<void>(serial);
    return {};
}

auto data_device_object::request_set_selection(object_id source, u32 serial) -> result<void> {
    static_cast<void>(serial);
    selection = source;
    return {};
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
    buffer = new_buffer;
    offset_x = x;
    offset_y = y;
    return {};
}

auto surface_object::request_damage(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    if (width > 0 && height > 0) {
        damage.push_back({x, y, width, height});
    }
    return {};
}

auto surface_object::request_frame(object_id callback) -> result<void> {
    if (callback == 0) {
        return std::unexpected(make_error(error_code::protocol, "frame callback id is zero"));
    }
    frame_callbacks.push_back(callback);
    return {};
}

auto surface_object::request_set_opaque_region(object_id region) -> result<void> {
    opaque_region = region;
    return {};
}

auto surface_object::request_set_input_region(object_id region) -> result<void> {
    input_region = region;
    return {};
}

auto surface_object::request_commit() -> result<void> {
    surface_commit_event event{.surface = id, .buffer = buffer};
    if (buffer != 0) {
        const buffer_object* committed = core->buffer(buffer);
        if (committed == nullptr) {
            return std::unexpected(make_error(error_code::protocol, "surface buffer is unknown"));
        }
        auto committed_pixels = core->buffer_pixels(buffer);
        if (!committed_pixels) {
            return std::unexpected(committed_pixels.error());
        }
        event.width = committed->width;
        event.height = committed->height;
        event.stride = committed->stride;
        event.format = committed->format;
        event.pixels = *committed_pixels;
    }
    core->notify_commit(event);

    for (const object_id callback : frame_callbacks) {
        if (auto sent = core->service().send_callback_done(callback, core->service().next_serial());
            !sent) {
            return std::unexpected(sent.error());
        }
    }
    frame_callbacks.clear();
    damage.clear();
    return {};
}

auto surface_object::request_set_buffer_transform(i32 transform) -> result<void> {
    buffer_transform = transform;
    return {};
}

auto surface_object::request_set_buffer_scale(i32 scale) -> result<void> {
    if (scale <= 0) {
        return std::unexpected(make_error(error_code::protocol, "buffer scale is invalid"));
    }
    buffer_scale = scale;
    return {};
}

auto surface_object::request_damage_buffer(i32 x, i32 y, i32 width, i32 height) -> result<void> {
    return request_damage(x, y, width, height);
}

auto surface_object::request_offset(i32 x, i32 y) -> result<void> {
    offset_x = x;
    offset_y = y;
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
    if (interface == generated::wl_subcompositor::name) {
        auto* object = store(subcompositors_, subcompositor_object{this, id});
        return add_object<subcompositor_object, generated::wl_subcompositor>(
            service_->connection_ref().objects(), id, *object);
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

auto core_protocol::create_output(object_id id, u32 version) -> result<void> {
    auto* object = store(outputs_, output_object{this, id, version});
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
    generated::wl_output::event_geometry geometry{};
    std::get<0>(geometry.arguments) = 0;
    std::get<1>(geometry.arguments) = 0;
    std::get<2>(geometry.arguments) = 0;
    std::get<3>(geometry.arguments) = 0;
    std::get<4>(geometry.arguments) = 0;
    std::get<5>(geometry.arguments) = "powerwm";
    std::get<6>(geometry.arguments) = "virtual";
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
    std::get<1>(mode.arguments) = static_cast<i32>(output_width_);
    std::get<2>(mode.arguments) = static_cast<i32>(output_height_);
    std::get<3>(mode.arguments) = static_cast<i32>(output_refresh_hz_ * 1000U);
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
    std::get<0>(scale.arguments) = 1;
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
        std::get<0>(name.arguments) = "powerwm-0";
        protocol_writer name_writer;
        if (auto encoded = generated::wl_output::event_name::encode(name_writer, id, name);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = service_->queue_event(name_writer); !queued) {
            return std::unexpected(queued.error());
        }
        generated::wl_output::event_description description{};
        std::get<0>(description.arguments) = "powerwm virtual output";
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
