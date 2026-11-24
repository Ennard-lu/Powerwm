// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <string_view>

namespace pwm::platform::seat {

class seat_device {
  public:
    seat_device() noexcept = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto device_id() const noexcept -> i32;

    auto destroy() const noexcept -> void;

  private:
    friend class session;

    seat_device(void* seat_handle, handle_t fd, i32 device_id) noexcept
        : seat_handle_(seat_handle), fd_(fd), device_id_(device_id) {
    }

    mutable void* seat_handle_ = nullptr;
    mutable handle_t fd_ = -1;
    mutable i32 device_id_ = -1;
};

class session {
  public:
    session() noexcept = default;

    [[nodiscard]] static auto create() noexcept -> result<session>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto is_enabled() const noexcept -> bool;

    [[nodiscard]] auto open_device(std::string_view path) noexcept -> result<seat_device>;

    [[nodiscard]] auto dispatch(i32 timeout_ms) noexcept -> result<void>;

    auto destroy() const noexcept -> void;

  private:
    session(void* handle, void* state) noexcept : handle_(handle), state_(state) {
    }

    mutable void* handle_ = nullptr;
    mutable void* state_ = nullptr;
};

}