// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

namespace pwm::platform::event {

class timer_fd {
  public:
    timer_fd() noexcept = default;

    [[nodiscard]] static auto create() noexcept -> result<timer_fd>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto arm_ms(u32 milliseconds) noexcept -> result<void>;

    [[nodiscard]] auto disarm() noexcept -> result<void>;

    [[nodiscard]] auto read_expirations() noexcept -> result<u64>;

    auto destroy() const noexcept -> void;

  private:
    explicit timer_fd(handle_t fd) noexcept : fd_(fd) {
    }

    mutable handle_t fd_ = -1;
};

}