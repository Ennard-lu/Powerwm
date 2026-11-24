// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <platform/wayland/fd.hpp>
#include <platform/wayland/mapped_region.hpp>
#include <wayland/wire.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pwm::wayland {

class display_service;
class core_protocol;

struct surface_commit_event {
    object_id surface = 0;
    object_id buffer = 0;
    i32 width = 0;
    i32 height = 0;
    i32 stride = 0;
    u32 format = 0;
    std::span<const std::byte> pixels;
};

using surface_commit_callback = auto (*)(void* context, const surface_commit_event& event) -> void;

struct subsurface_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id surface = 0;
    object_id parent = 0;
    i32 x = 0;
    i32 y = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_position(i32 x, i32 y) -> result<void>;
    [[nodiscard]] auto request_place_above(object_id sibling) -> result<void>;
    [[nodiscard]] auto request_place_below(object_id sibling) -> result<void>;
    [[nodiscard]] auto request_set_sync() -> result<void>;
    [[nodiscard]] auto request_set_desync() -> result<void>;
};

struct subcompositor_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_subsurface(object_id subsurface, object_id surface,
                                              object_id parent) -> result<void>;
};

struct data_offer_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_accept(u32 serial, std::string_view mime) -> result<void>;
    [[nodiscard]] auto request_receive(std::string_view mime, handle_t fd) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_finish() -> result<void>;
    [[nodiscard]] auto request_set_actions(u32 source_actions, u32 action) -> result<void>;
};

struct data_source_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    std::vector<std::string> mime_types;
    u32 actions = 0;

    [[nodiscard]] auto request_offer(std::string_view mime) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_actions(u32 actions) -> result<void>;
};

struct data_device_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id seat = 0;
    object_id selection = 0;

    [[nodiscard]] auto request_start_drag(object_id source, object_id origin, object_id icon,
                                          u32 serial) -> result<void>;
    [[nodiscard]] auto request_set_selection(object_id source, u32 serial) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct data_device_manager_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_create_data_source(object_id source) -> result<void>;
    [[nodiscard]] auto request_get_data_device(object_id device, object_id seat) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct output_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_release() -> result<void>;
};

struct pointer_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_set_cursor(u32 serial, object_id surface, i32 hotspot_x,
                                          i32 hotspot_y) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct keyboard_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_release() -> result<void>;
};

struct touch_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_release() -> result<void>;
};

struct seat_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_get_pointer(object_id pointer) -> result<void>;
    [[nodiscard]] auto request_get_keyboard(object_id keyboard) -> result<void>;
    [[nodiscard]] auto request_get_touch(object_id touch) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct compositor_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_create_surface(object_id surface) -> result<void>;
    [[nodiscard]] auto request_create_region(object_id region) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct region_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    std::vector<base::recti> rectangles;

    [[nodiscard]] auto request_add(i32 x, i32 y, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_subtract(i32 x, i32 y, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
};

struct buffer_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id pool = 0;
    i32 offset = 0;
    i32 width = 0;
    i32 height = 0;
    i32 stride = 0;
    u32 format = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
};

struct shm_pool_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    platform::wayland::file_descriptor fd;
    i32 size = 0;
    platform::wayland::mapped_region mapping;

    [[nodiscard]] auto request_create_buffer(object_id buffer, i32 offset, i32 width, i32 height,
                                             i32 stride, u32 format) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_resize(i32 size) -> result<void>;
};

struct shm_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_create_pool(object_id pool, handle_t fd, i32 size) -> result<void>;
    [[nodiscard]] auto request_release() -> result<void>;
};

struct surface_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id buffer = 0;
    object_id opaque_region = 0;
    object_id input_region = 0;
    i32 offset_x = 0;
    i32 offset_y = 0;
    i32 buffer_scale = 1;
    i32 buffer_transform = 0;
    std::vector<base::recti> damage;
    std::vector<object_id> frame_callbacks;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_attach(object_id buffer, i32 x, i32 y) -> result<void>;
    [[nodiscard]] auto request_damage(i32 x, i32 y, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_frame(object_id callback) -> result<void>;
    [[nodiscard]] auto request_set_opaque_region(object_id region) -> result<void>;
    [[nodiscard]] auto request_set_input_region(object_id region) -> result<void>;
    [[nodiscard]] auto request_commit() -> result<void>;
    [[nodiscard]] auto request_set_buffer_transform(i32 transform) -> result<void>;
    [[nodiscard]] auto request_set_buffer_scale(i32 scale) -> result<void>;
    [[nodiscard]] auto request_damage_buffer(i32 x, i32 y, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_offset(i32 x, i32 y) -> result<void>;
    [[nodiscard]] auto request_get_release(object_id callback) -> result<void>;
};

class core_protocol {
  public:
    explicit core_protocol(display_service& service) noexcept;

    [[nodiscard]] auto bind_global(std::string_view interface, object_id id, u32 version)
        -> result<void>;

    [[nodiscard]] auto create_surface(object_id id) -> result<void>;
    [[nodiscard]] auto create_region(object_id id) -> result<void>;
    [[nodiscard]] auto create_pool(object_id id, handle_t fd, i32 size) -> result<void>;
    [[nodiscard]] auto create_buffer(object_id id, object_id pool, i32 offset, i32 width,
                                     i32 height, i32 stride, u32 format) -> result<void>;

    auto configure_output(u32 width, u32 height, u32 refresh_hz) noexcept -> void;

    [[nodiscard]] auto create_output(object_id id, u32 version) -> result<void>;
    [[nodiscard]] auto create_seat(object_id id, u32 version) -> result<void>;

    [[nodiscard]] auto surface(object_id id) noexcept -> surface_object*;

    [[nodiscard]] auto buffer(object_id id) noexcept -> buffer_object*;

    [[nodiscard]] auto buffer_pixels(object_id id) noexcept -> result<std::span<const std::byte>>;

    [[nodiscard]] auto keyboards() const noexcept -> std::span<const object_id>;

    [[nodiscard]] auto pointers() const noexcept -> std::span<const object_id>;

    [[nodiscard]] auto touches() const noexcept -> std::span<const object_id>;

    [[nodiscard]] auto send_keyboard_keymap(object_id id) -> result<void>;

    auto set_surface_commit_callback(surface_commit_callback callback, void* context) noexcept
        -> void;

    [[nodiscard]] auto service() noexcept -> display_service&;

    auto notify_commit(const surface_commit_event& event) noexcept -> void;

  private:
    friend struct seat_object;
    friend struct subcompositor_object;
    friend struct data_device_manager_object;

    [[nodiscard]] auto send_shm_formats(object_id id) -> result<void>;
    [[nodiscard]] auto send_output_events(object_id id, u32 version) -> result<void>;
    [[nodiscard]] auto send_seat_events(object_id id, u32 version) -> result<void>;

    template <typename T>
    auto store(std::vector<std::unique_ptr<T>>& objects, T object) -> T* {
        auto held = std::make_unique<T>(std::move(object));
        T* pointer = held.get();
        objects.push_back(std::move(held));
        return pointer;
    }

    display_service* service_;
    std::vector<std::unique_ptr<compositor_object>> compositors_;
    std::vector<std::unique_ptr<region_object>> regions_;
    std::vector<std::unique_ptr<buffer_object>> buffers_;
    std::vector<std::unique_ptr<shm_pool_object>> pools_;
    std::vector<std::unique_ptr<shm_object>> shms_;
    std::vector<std::unique_ptr<surface_object>> surfaces_;
    std::vector<std::unique_ptr<output_object>> outputs_;
    std::vector<std::unique_ptr<seat_object>> seats_;
    std::vector<std::unique_ptr<pointer_object>> pointers_;
    std::vector<std::unique_ptr<keyboard_object>> keyboards_;
    std::vector<std::unique_ptr<touch_object>> touches_;
    std::vector<std::unique_ptr<subcompositor_object>> subcompositors_;
    std::vector<std::unique_ptr<subsurface_object>> subsurfaces_;
    std::vector<std::unique_ptr<data_device_manager_object>> data_managers_;
    std::vector<std::unique_ptr<data_device_object>> data_devices_;
    std::vector<std::unique_ptr<data_source_object>> data_sources_;
    std::vector<std::unique_ptr<data_offer_object>> data_offers_;
    std::vector<object_id> pointer_ids_;
    std::vector<object_id> keyboard_ids_;
    std::vector<object_id> touch_ids_;
    u32 output_width_ = 1280;
    u32 output_height_ = 720;
    u32 output_refresh_hz_ = 60;
    surface_commit_callback commit_callback_ = nullptr;
    void* commit_context_ = nullptr;
};

}
