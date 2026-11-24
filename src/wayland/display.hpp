// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/connection.hpp>
#include <wayland/object_table.hpp>

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

    [[nodiscard]] auto globals() const noexcept -> const std::vector<registry_global>&;

    [[nodiscard]] auto next_serial() noexcept -> u32;

    [[nodiscard]] auto send_registry_globals(object_id registry) -> result<void>;

  private:
    friend struct display_object;
    friend struct registry_object;

    [[nodiscard]] auto send_callback_done(object_id callback, u32 data) -> result<void>;

    connection* connection_;
    display_object display_;
    std::vector<std::unique_ptr<registry_object>> registries_;
    std::vector<registry_global> globals_;
    u32 next_global_name_ = 1;
    u32 next_serial_ = 1;
};

}
