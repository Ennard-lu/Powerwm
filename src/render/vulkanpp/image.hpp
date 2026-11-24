// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/device.hpp>

namespace pwm::vulkanpp {

class image {
  public:
    image() noexcept = default;

    image(const image&) = delete;
    auto operator=(const image&) -> image& = delete;

    image(image&&) noexcept = default;
    auto operator=(image&&) noexcept -> image& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_image(const device& device, u32 width_px, u32 height_px) noexcept
        -> result<image>;

    image(void* device, void* handle, void* memory) noexcept
        : device_(device), handle_(handle), memory_(memory) {
    }

    mutable void* device_ = nullptr;
    mutable void* handle_ = nullptr;
    mutable void* memory_ = nullptr;
};

class buffer {
  public:
    buffer() noexcept = default;

    buffer(const buffer&) = delete;
    auto operator=(const buffer&) -> buffer& = delete;

    buffer(buffer&&) noexcept = default;
    auto operator=(buffer&&) noexcept -> buffer& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    [[nodiscard]] auto mapped_data() const noexcept -> void*;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_readback_buffer(const device& device, u64 size_bytes) noexcept
        -> result<buffer>;

    buffer(void* device, void* handle, void* memory, void* mapped) noexcept
        : device_(device), handle_(handle), memory_(memory), mapped_(mapped) {
    }

    mutable void* device_ = nullptr;
    mutable void* handle_ = nullptr;
    mutable void* memory_ = nullptr;
    mutable void* mapped_ = nullptr;
};

[[nodiscard]] auto create_image(const device& device, u32 width_px, u32 height_px) noexcept
    -> result<image>;

[[nodiscard]] auto create_readback_buffer(const device& device, u64 size_bytes) noexcept
    -> result<buffer>;

}