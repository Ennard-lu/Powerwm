// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <span>
#include <string>
#include <string_view>

namespace pwm::platform::wayland {

struct receive_result {
    usize bytes = 0;
    usize fds = 0;
    bool closed = false;
    bool would_block = false;
};

struct send_result {
    usize bytes = 0;
    bool would_block = false;
};

class stream {
  public:
    stream() noexcept = default;

    stream(const stream&) = delete;
    auto operator=(const stream&) -> stream& = delete;

    stream(stream&& other) noexcept;
    auto operator=(stream&& other) noexcept -> stream&;
    ~stream() noexcept;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto receive(std::span<std::byte> data, std::span<handle_t> fds) noexcept
        -> result<receive_result>;

    [[nodiscard]] auto send(std::span<const std::byte> data, std::span<const handle_t> fds) noexcept
        -> result<send_result>;

    auto destroy() const noexcept -> void;

  private:
    friend class listener;
    friend auto connect_stream(std::string_view path) noexcept -> result<stream>;

    explicit stream(handle_t fd) noexcept : fd_(fd) {
    }

    mutable handle_t fd_ = -1;
};

class listener {
  public:
    listener() noexcept = default;

    listener(const listener&) = delete;
    auto operator=(const listener&) -> listener& = delete;

    listener(listener&& other) noexcept;
    auto operator=(listener&& other) noexcept -> listener&;
    ~listener() noexcept;

    [[nodiscard]] static auto create(std::string_view path) noexcept -> result<listener>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto accept() noexcept -> result<stream>;

    auto destroy() const noexcept -> void;

  private:
    listener(handle_t fd, std::string path) noexcept : fd_(fd), path_(std::move(path)) {
    }

    mutable handle_t fd_ = -1;
    mutable std::string path_;
};

[[nodiscard]] auto connect_stream(std::string_view path) noexcept -> result<stream>;

}
