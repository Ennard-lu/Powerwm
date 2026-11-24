// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/connection.hpp>
#include <wayland/core.hpp>
#include <wayland/object_table.hpp>
#include <wayland/xdg_shell.hpp>

#include <memory>
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

    auto set_surface_commit_callback(surface_commit_callback callback, void* context) noexcept
        -> void;

  private:
    friend struct display_object;
    friend struct registry_object;

    [[nodiscard]] auto send_delete_id(object_id id) -> result<void>;

    connection* connection_;
    display_object display_;
    core_protocol core_;
    xdg_shell_protocol xdg_;
    std::vector<std::unique_ptr<registry_object>> registries_;
    std::vector<registry_global> globals_;
    u32 next_global_name_ = 1;
    u32 next_serial_ = 1;
};

}
