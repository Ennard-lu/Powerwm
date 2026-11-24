// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <platform/wayland/fd.hpp>

#include <span>
#include <string_view>

namespace pwm::platform::wayland {

class mapped_region {
  public:
    mapped_region() noexcept = default;

    mapped_region(const mapped_region&) = delete;
    auto operator=(const mapped_region&) -> mapped_region& = delete;

    mapped_region(mapped_region&& other) noexcept;
    auto operator=(mapped_region&& other) noexcept -> mapped_region&;
    ~mapped_region() noexcept;

    [[nodiscard]] static auto create(handle_t fd, usize offset, usize size,
                                     bool writable = false) noexcept -> result<mapped_region>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto data() const noexcept -> std::span<const std::byte>;

    [[nodiscard]] auto writable_data() noexcept -> std::span<std::byte>;

    [[nodiscard]] auto size() const noexcept -> usize;

    auto destroy() const noexcept -> void;

  private:
    mapped_region(void* address, usize size, bool writable) noexcept
        : address_(address), size_(size), writable_(writable) {
    }

    mutable void* address_ = nullptr;
    mutable usize size_ = 0;
    bool writable_ = false;
};

[[nodiscard]] auto create_anonymous_file(std::string_view name, usize size) noexcept
    -> result<file_descriptor>;

}
