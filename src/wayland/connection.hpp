// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <platform/wayland/socket.hpp>
#include <wayland/object_table.hpp>
#include <wayland/wire.hpp>

#include <vector>

namespace pwm::wayland {

class connection {
  public:
    connection() noexcept = default;

    connection(const connection&) = delete;
    auto operator=(const connection&) -> connection& = delete;

    connection(connection&&) noexcept = default;
    auto operator=(connection&&) noexcept -> connection& = default;

    explicit connection(platform::wayland::stream stream) noexcept;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto objects() noexcept -> object_table&;

    [[nodiscard]] auto objects() const noexcept -> const object_table&;

    [[nodiscard]] auto receive_available() noexcept -> result<void>;

    [[nodiscard]] auto dispatch_available() noexcept -> result<usize>;

    [[nodiscard]] auto queue_message(const protocol_writer& writer) noexcept -> result<void>;

    [[nodiscard]] auto flush() noexcept -> result<void>;

    [[nodiscard]] auto closed() const noexcept -> bool;

    auto disconnect() noexcept -> void;

  private:
    platform::wayland::stream stream_;
    object_table objects_;
    std::vector<std::byte> input_;
    std::vector<handle_t> input_fds_;
    std::vector<std::byte> output_;
    std::vector<handle_t> output_fds_;
    usize output_offset_ = 0;
    bool output_fds_sent_ = false;
    bool closed_ = false;
};

}
