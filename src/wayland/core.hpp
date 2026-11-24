// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <platform/wayland/fd.hpp>
#include <platform/wayland/mapped_region.hpp>
#include <wayland/wire.hpp>

#include <memory>
#include <optional>
#include <span>
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
    bool is_dmabuf = false;
    handle_t fd = -1;
    u64 modifier = 0;
};

using surface_commit_callback = auto (*)(void* context, const surface_commit_event& event) -> void;

struct presentation_feedback_object {
    core_protocol* core = nullptr;
    object_id id = 0;
};

struct presentation_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_feedback(object_id feedback, object_id surface) -> result<void>;
};

struct activation_token_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 serial = 0;
    object_id surface = 0;
    std::string app_id;

    [[nodiscard]] auto request_set_serial(u32 serial, object_id seat) -> result<void>;
    [[nodiscard]] auto request_set_app_id(std::string_view app_id) -> result<void>;
    [[nodiscard]] auto request_set_surface(object_id surface) -> result<void>;
    [[nodiscard]] auto request_commit() -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
};

struct activation_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_activation_token(object_id token) -> result<void>;
    [[nodiscard]] auto request_activate(std::string_view token, object_id surface) -> result<void>;
};

struct viewport_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id surface = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_source(f64 x, f64 y, f64 width, f64 height) -> result<void>;
    [[nodiscard]] auto request_set_destination(i32 width, i32 height) -> result<void>;
};

struct viewporter_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_viewport(object_id viewport, object_id surface) -> result<void>;
};

struct fractional_scale_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
};

struct fractional_scale_manager_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_fractional_scale(object_id fractional_scale, object_id surface)
        -> result<void>;
};

struct toplevel_decoration_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id toplevel = 0;
    u32 mode = 1;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_mode(u32 mode) -> result<void>;
    [[nodiscard]] auto request_unset_mode() -> result<void>;
};

struct decoration_manager_object {
    core_protocol* core = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_toplevel_decoration(object_id decoration, object_id toplevel)
        -> result<void>;
};

struct xdg_output_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_destroy() -> result<void>;
};

struct xdg_output_manager_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_get_xdg_output(object_id xdg_output, object_id output)
        -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
};

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
    u32 source_client = 0;
    object_id source_object = 0;
    std::vector<std::string> mime_types;

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

struct linux_buffer_params_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    platform::wayland::file_descriptor fd;
    u32 offset = 0;
    u32 stride = 0;
    u64 modifier = 0;
    u32 plane_count = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_add(handle_t fd, u32 plane, u32 offset, u32 stride, u32 modifier_hi,
                                   u32 modifier_lo) -> result<void>;
    [[nodiscard]] auto request_create(i32 width, i32 height, u32 format, u32 flags) -> result<void>;
    [[nodiscard]] auto request_create_immed(object_id buffer, i32 width, i32 height, u32 format,
                                            u32 flags) -> result<void>;
    [[nodiscard]] auto request_set_sampling_device(std::span<const std::byte> device)
        -> result<void>;
};

struct linux_dmabuf_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_create_params(object_id params) -> result<void>;
    [[nodiscard]] auto request_get_default_feedback(object_id feedback) -> result<void>;
    [[nodiscard]] auto request_get_surface_feedback(object_id feedback, object_id surface)
        -> result<void>;
};

struct layer_surface_geometry {
    base::recti bounds;
    u32 layer = 0;
    i32 exclusive = 0;
    bool keyboard_interactive = false;
};

struct layer_surface_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    object_id surface = 0;
    object_id output = 0;
    u32 layer = 0;
    std::string space;
    u32 anchor = 0;
    u32 width = 0;
    u32 height = 0;
    i32 exclusive = 0;
    i32 margin_top = 0;
    i32 margin_right = 0;
    i32 margin_bottom = 0;
    i32 margin_left = 0;
    u32 keyboard = 0;
    bool configured = false;
    u32 configure_serial = 0;

    [[nodiscard]] auto request_set_size(u32 width, u32 height) -> result<void>;
    [[nodiscard]] auto request_set_anchor(u32 anchor) -> result<void>;
    [[nodiscard]] auto request_set_exclusive_zone(i32 zone) -> result<void>;
    [[nodiscard]] auto request_set_margin(i32 top, i32 right, i32 bottom, i32 left) -> result<void>;
    [[nodiscard]] auto request_set_keyboard_interactivity(u32 value) -> result<void>;
    [[nodiscard]] auto request_get_popup(object_id popup) -> result<void>;
    [[nodiscard]] auto request_ack_configure(u32 serial) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_layer(u32 layer) -> result<void>;
};

struct layer_shell_object {
    core_protocol* core = nullptr;
    object_id id = 0;
    u32 version = 1;

    [[nodiscard]] auto request_get_layer_surface(object_id id, object_id surface, object_id output,
                                                 u32 layer, std::string_view space) -> result<void>;
    [[nodiscard]] auto request_destroy() -> result<void>;
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
    bool is_dmabuf = false;
    bool server_created = false;
    platform::wayland::file_descriptor dmabuf_fd;
    u64 modifier = 0;

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
    [[nodiscard]] auto create_dmabuf_buffer(object_id id, platform::wayland::file_descriptor fd,
                                            i32 width, i32 height, u32 stride, u32 format,
                                            u64 modifier, bool server_created) -> result<void>;
    [[nodiscard]] auto allocate_server_object_id() noexcept -> object_id;
    [[nodiscard]] auto create_linux_buffer_params(object_id id) -> result<void>;
    [[nodiscard]] auto create_layer_shell(object_id id, u32 version) -> result<void>;
    [[nodiscard]] auto create_layer_surface(object_id id, object_id surface, object_id output,
                                            u32 layer, std::string_view space) -> result<void>;
    [[nodiscard]] auto layer_geometry(object_id surface, u32 output_width, u32 output_height,
                                      i32 buffer_width, i32 buffer_height) const
        -> std::optional<layer_surface_geometry>;
    [[nodiscard]] auto configure_layer_surface(object_id surface) -> result<void>;
    [[nodiscard]] auto send_linux_buffer_created(object_id params, object_id buffer)
        -> result<void>;
    [[nodiscard]] auto send_linux_buffer_failed(object_id params) -> result<void>;

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

    [[nodiscard]] auto create_data_offer(u32 source_client, object_id source_object,
                                         std::span<const std::string> mime_types, u32 actions)
        -> result<object_id>;
    [[nodiscard]] auto data_source_mime_types(object_id source) const
        -> std::span<const std::string>;
    [[nodiscard]] auto data_source_actions(object_id source) const noexcept -> u32;
    [[nodiscard]] auto send_selection(object_id offer) -> result<void>;
    [[nodiscard]] auto send_data_source_send(object_id source, std::string_view mime, handle_t fd)
        -> result<void>;
    [[nodiscard]] auto send_data_source_target(object_id source, std::string_view mime)
        -> result<void>;
    [[nodiscard]] auto send_data_source_cancelled(object_id source) -> result<void>;
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

    auto set_surface_commit_callback(surface_commit_callback callback, void* context) noexcept
        -> void;

    [[nodiscard]] auto service() noexcept -> display_service&;

    auto notify_commit(const surface_commit_event& event) noexcept -> void;

  private:
    friend struct seat_object;
    friend struct subcompositor_object;
    friend struct xdg_output_manager_object;
    friend struct viewporter_object;
    friend struct presentation_object;
    friend struct activation_object;
    friend struct fractional_scale_manager_object;
    friend struct decoration_manager_object;
    friend struct data_device_manager_object;
    friend struct data_offer_object;
    friend struct data_source_object;
    friend struct data_device_object;
    friend struct linux_buffer_params_object;
    friend struct linux_dmabuf_object;
    friend struct layer_surface_object;
    friend struct layer_shell_object;

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
    std::vector<std::unique_ptr<presentation_object>> presentations_;
    std::vector<std::unique_ptr<presentation_feedback_object>> presentation_feedbacks_;
    std::vector<std::unique_ptr<activation_object>> activations_;
    std::vector<std::unique_ptr<activation_token_object>> activation_tokens_;
    std::vector<std::unique_ptr<viewporter_object>> viewporters_;
    std::vector<std::unique_ptr<viewport_object>> viewports_;
    std::vector<std::unique_ptr<fractional_scale_manager_object>> fractional_scale_managers_;
    std::vector<std::unique_ptr<fractional_scale_object>> fractional_scales_;
    std::vector<std::unique_ptr<decoration_manager_object>> decoration_managers_;
    std::vector<std::unique_ptr<toplevel_decoration_object>> toplevel_decorations_;
    std::vector<std::unique_ptr<xdg_output_manager_object>> xdg_output_managers_;
    std::vector<std::unique_ptr<xdg_output_object>> xdg_outputs_;
    std::vector<std::unique_ptr<subcompositor_object>> subcompositors_;
    std::vector<std::unique_ptr<subsurface_object>> subsurfaces_;
    std::vector<std::unique_ptr<data_device_manager_object>> data_managers_;
    std::vector<std::unique_ptr<data_device_object>> data_devices_;
    std::vector<std::unique_ptr<data_source_object>> data_sources_;
    std::vector<std::unique_ptr<data_offer_object>> data_offers_;
    std::vector<std::unique_ptr<linux_dmabuf_object>> linux_dmabufs_;
    std::vector<std::unique_ptr<linux_buffer_params_object>> linux_buffer_params_;
    std::vector<std::unique_ptr<layer_shell_object>> layer_shells_;
    std::vector<std::unique_ptr<layer_surface_object>> layer_surfaces_;
    std::vector<object_id> pointer_ids_;
    std::vector<object_id> keyboard_ids_;
    std::vector<object_id> touch_ids_;
    u32 output_width_ = 1280;
    u32 output_height_ = 720;
    u32 output_refresh_hz_ = 60;
    surface_commit_callback commit_callback_ = nullptr;
    void* commit_context_ = nullptr;
    object_id next_server_id_ = 0xff000000;
};

}
