// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <optional>
#include <string>

namespace pwm::platform::udev {

struct device_event {
    std::string action;
    std::string subsystem;
    std::string devnode;
    bool hotplug = false;
};

class monitor {
  public:
    monitor() noexcept = default;

    [[nodiscard]] static auto create() noexcept -> result<monitor>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto receive() noexcept -> std::optional<device_event>;

    auto destroy() const noexcept -> void;

  private:
    monitor(void* context, void* handle, handle_t fd) noexcept
        : context_(context), handle_(handle), fd_(fd) {
    }

    mutable void* context_ = nullptr;
    mutable void* handle_ = nullptr;
    mutable handle_t fd_ = -1;
};

}