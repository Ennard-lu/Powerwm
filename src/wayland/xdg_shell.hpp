// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <wayland/wire.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pwm::wayland {

class display_service;
class xdg_shell_protocol;

struct xdg_positioner_object {
    xdg_shell_protocol* shell = nullptr;
    object_id id = 0;
    base::recti anchor_rect{};
    base::recti size{};
    u32 anchor = 0;
    u32 gravity = 0;
    u32 constraint_adjustment = 0;
    i32 offset_x = 0;
    i32 offset_y = 0;
    i32 parent_width = 0;
    i32 parent_height = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_size(i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_set_anchor_rect(i32 x, i32 y, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_set_anchor(u32 anchor) -> result<void>;
    [[nodiscard]] auto request_set_gravity(u32 gravity) -> result<void>;
    [[nodiscard]] auto request_set_constraint_adjustment(u32 adjustment) -> result<void>;
    [[nodiscard]] auto request_set_offset(i32 x, i32 y) -> result<void>;
    [[nodiscard]] auto request_set_reactive() -> result<void>;
    [[nodiscard]] auto request_set_parent_size(i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_set_parent_configure(u32 serial) -> result<void>;
};

struct xdg_toplevel_object {
    xdg_shell_protocol* shell = nullptr;
    object_id id = 0;
    object_id parent = 0;
    std::string title;
    std::string app_id;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_set_parent(object_id parent) -> result<void>;
    [[nodiscard]] auto request_set_title(std::string_view title) -> result<void>;
    [[nodiscard]] auto request_set_app_id(std::string_view app_id) -> result<void>;
    [[nodiscard]] auto request_show_window_menu(object_id seat, u32 serial, i32 x, i32 y)
        -> result<void>;
    [[nodiscard]] auto request_move(object_id seat, u32 serial) -> result<void>;
    [[nodiscard]] auto request_resize(object_id seat, u32 serial, u32 edges) -> result<void>;
    [[nodiscard]] auto request_set_max_size(i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_set_min_size(i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto request_set_maximized() -> result<void>;
    [[nodiscard]] auto request_unset_maximized() -> result<void>;
    [[nodiscard]] auto request_set_fullscreen(object_id output) -> result<void>;
    [[nodiscard]] auto request_unset_fullscreen() -> result<void>;
    [[nodiscard]] auto request_set_minimized() -> result<void>;
};

struct xdg_popup_object {
    xdg_shell_protocol* shell = nullptr;
    object_id id = 0;
    object_id parent = 0;
    object_id positioner = 0;
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;
    bool grabbed = false;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_grab(object_id seat, u32 serial) -> result<void>;
    [[nodiscard]] auto request_reposition(object_id positioner, u32 token) -> result<void>;
};

struct xdg_surface_object {
    xdg_shell_protocol* shell = nullptr;
    object_id id = 0;
    object_id surface = 0;
    object_id role = 0;
    base::recti window_geometry{};
    u32 configure_serial = 0;
    bool configured = false;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_get_toplevel(object_id toplevel) -> result<void>;
    [[nodiscard]] auto request_get_popup(object_id popup, object_id parent, object_id positioner)
        -> result<void>;
    [[nodiscard]] auto request_set_window_geometry(i32 x, i32 y, i32 width, i32 height)
        -> result<void>;
    [[nodiscard]] auto request_ack_configure(u32 serial) -> result<void>;
};

struct xdg_wm_base_object {
    xdg_shell_protocol* shell = nullptr;
    object_id id = 0;

    [[nodiscard]] auto request_destroy() -> result<void>;
    [[nodiscard]] auto request_create_positioner(object_id positioner) -> result<void>;
    [[nodiscard]] auto request_get_xdg_surface(object_id xdg_surface, object_id surface)
        -> result<void>;
    [[nodiscard]] auto request_pong(u32 serial) -> result<void>;
};

struct popup_geometry {
    object_id parent_surface = 0;
    base::recti bounds{};
};

class xdg_shell_protocol {
  public:
    explicit xdg_shell_protocol(display_service& service) noexcept;

    [[nodiscard]] auto bind_global(std::string_view interface, object_id id, u32 version)
        -> result<void>;

    [[nodiscard]] auto create_positioner(object_id id) -> result<void>;
    [[nodiscard]] auto create_surface(object_id id, object_id surface) -> result<void>;
    [[nodiscard]] auto create_toplevel(object_id id, xdg_surface_object& surface) -> result<void>;
    [[nodiscard]] auto create_popup(object_id id, object_id parent, object_id positioner,
                                    xdg_surface_object& surface) -> result<void>;

    [[nodiscard]] auto send_toplevel_configure(object_id id, i32 width, i32 height) -> result<void>;
    [[nodiscard]] auto send_surface_configure(object_id id, u32 serial) -> result<void>;
    [[nodiscard]] auto send_popup_configure(object_id id, i32 x, i32 y, i32 width, i32 height)
        -> result<void>;
    [[nodiscard]] auto send_popup_done(object_id id) -> result<void>;
    [[nodiscard]] auto send_repositioned(object_id id, u32 token) -> result<void>;

    [[nodiscard]] auto service() noexcept -> display_service&;

    [[nodiscard]] auto surface_popup_geometry(object_id popup_surface) const
        -> std::optional<popup_geometry>;

    auto dismiss_grabbed_popups(object_id hit_surface) -> void;

    auto discard_object(object_id id) noexcept -> void;

  private:
    friend struct xdg_popup_object;

    template <typename T>
    auto store(std::vector<std::unique_ptr<T>>& objects, T object) -> T* {
        auto held = std::make_unique<T>(std::move(object));
        T* pointer = held.get();
        objects.push_back(std::move(held));
        return pointer;
    }

    display_service* service_;
    std::vector<std::unique_ptr<xdg_wm_base_object>> wm_bases_;
    std::vector<std::unique_ptr<xdg_positioner_object>> positioners_;
    std::vector<std::unique_ptr<xdg_surface_object>> surfaces_;
    std::vector<std::unique_ptr<xdg_toplevel_object>> toplevels_;
    std::vector<std::unique_ptr<xdg_popup_object>> popups_;
};

}
