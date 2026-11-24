// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/types.hpp>

namespace pwm::platform::wayland {

class file_descriptor {
  public:
    file_descriptor() noexcept = default;

    file_descriptor(const file_descriptor&) = delete;
    auto operator=(const file_descriptor&) -> file_descriptor& = delete;

    file_descriptor(file_descriptor&& other) noexcept;
    auto operator=(file_descriptor&& other) noexcept -> file_descriptor&;
    ~file_descriptor() noexcept;

    [[nodiscard]] static auto adopt(handle_t fd) noexcept -> file_descriptor;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    auto destroy() const noexcept -> void;

  private:
    explicit file_descriptor(handle_t fd) noexcept : fd_(fd) {
    }

    mutable handle_t fd_ = -1;
};

}
