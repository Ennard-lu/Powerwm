// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace pwm::wayland {

using object_id = u32;

struct message_header {
    object_id object = 0;
    u16 opcode = 0;
    u16 size = 0;
};

static_assert(sizeof(message_header) == 8);

struct untyped_new_id {
    object_id id = 0;
    std::string_view interface;
};

class protocol_reader {
  public:
    protocol_reader(std::span<const std::byte> data, std::span<const handle_t> fds = {}) noexcept;

    [[nodiscard]] auto remaining() const noexcept -> usize;

    [[nodiscard]] auto read_u32() noexcept -> result<u32>;
    [[nodiscard]] auto read_i32() noexcept -> result<i32>;
    [[nodiscard]] auto read_fixed() noexcept -> result<f64>;
    [[nodiscard]] auto read_string() noexcept -> result<std::string_view>;
    [[nodiscard]] auto read_array() noexcept -> result<std::span<const std::byte>>;
    [[nodiscard]] auto read_object() noexcept -> result<object_id>;
    [[nodiscard]] auto read_new_id() noexcept -> result<object_id>;
    [[nodiscard]] auto read_untyped_new_id() noexcept -> result<untyped_new_id>;
    [[nodiscard]] auto read_fd() noexcept -> result<handle_t>;

  private:
    auto take(usize size) noexcept -> result<std::span<const std::byte>>;
    auto align_to_four() noexcept -> bool;

    std::span<const std::byte> data_;
    std::span<const handle_t> fds_;
    usize position_ = 0;
    usize fd_position_ = 0;
};

class protocol_writer {
  public:
    protocol_writer() = default;

    [[nodiscard]] auto begin_message(object_id object, u16 opcode) noexcept -> result<void>;
    [[nodiscard]] auto end_message() noexcept -> result<void>;

    [[nodiscard]] auto write_u32(u32 value) noexcept -> result<void>;
    [[nodiscard]] auto write_i32(i32 value) noexcept -> result<void>;
    [[nodiscard]] auto write_fixed(f64 value) noexcept -> result<void>;
    [[nodiscard]] auto write_string(std::string_view value) noexcept -> result<void>;
    [[nodiscard]] auto write_array(std::span<const std::byte> value) noexcept -> result<void>;
    [[nodiscard]] auto write_object(object_id value) noexcept -> result<void>;
    [[nodiscard]] auto write_new_id(object_id value) noexcept -> result<void>;
    [[nodiscard]] auto write_untyped_new_id(object_id value, std::string_view interface) noexcept
        -> result<void>;
    [[nodiscard]] auto write_fd(handle_t value) noexcept -> result<void>;

    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte>;
    [[nodiscard]] auto fds() const noexcept -> std::span<const handle_t>;

    auto clear() noexcept -> void;

  private:
    auto write_raw(std::span<const std::byte> value) noexcept -> result<void>;
    auto pad_to_four() noexcept -> result<void>;

    std::vector<std::byte> data_;
    std::vector<handle_t> fds_;
    usize message_start_ = 0;
    bool message_open_ = false;
};

[[nodiscard]] auto decode_header(std::span<const std::byte> data) noexcept
    -> result<message_header>;

}
