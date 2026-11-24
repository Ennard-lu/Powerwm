// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

namespace pwm::platform::event {

class event_fd {
  public:
    event_fd() noexcept = default;

    [[nodiscard]] static auto create(u32 initial_value) noexcept -> result<event_fd>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto write(u64 value) noexcept -> result<void>;

    [[nodiscard]] auto read() noexcept -> result<u64>;

    auto destroy() const noexcept -> void;

  private:
    explicit event_fd(handle_t fd) noexcept : fd_(fd) {
    }

    mutable handle_t fd_ = -1;
};

}