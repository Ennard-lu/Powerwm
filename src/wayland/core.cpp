// SPDX-License-Identifier: Apache-2.0

#include <wayland/core.hpp>
#include <wayland/display.hpp>

#include <algorithm>
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
    auto* object = store(
        pools_, shm_pool_object{this, id, platform::wayland::file_descriptor::adopt(fd), size});
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

auto core_protocol::service() noexcept -> display_service& {
    return *service_;
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
