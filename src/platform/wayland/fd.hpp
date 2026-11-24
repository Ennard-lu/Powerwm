// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <span>

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

    [[nodiscard]] auto read(std::span<std::byte> buffer) const noexcept -> result<usize>;

    [[nodiscard]] auto write(std::span<const std::byte> buffer) const noexcept -> result<usize>;

    auto destroy() const noexcept -> void;

  private:
    explicit file_descriptor(handle_t fd) noexcept : fd_(fd) {
    }

    mutable handle_t fd_ = -1;
};

struct file_descriptor_pair {
    file_descriptor read;
    file_descriptor write;
};

[[nodiscard]] auto create_pipe() noexcept -> result<file_descriptor_pair>;

}
