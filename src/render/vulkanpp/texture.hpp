// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/device.hpp>

#include <cstddef>
#include <span>

namespace pwm::vulkanpp {

enum class texture_format : u8 {
    rgba8,
    bgra8,
};

struct texture_data {
    std::span<const std::byte> pixels;
    u32 stride_bytes = 0;
    texture_format format = texture_format::rgba8;
};

class texture {
  public:
    texture() noexcept = default;

    texture(const texture&) = delete;
    auto operator=(const texture&) -> texture& = delete;

    texture(texture&&) noexcept = default;
    auto operator=(texture&&) noexcept -> texture& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto width_px() const noexcept -> u32;

    [[nodiscard]] auto height_px() const noexcept -> u32;

    [[nodiscard]] auto native_view() const noexcept -> void*;

    [[nodiscard]] auto native_sampler() const noexcept -> void*;

    [[nodiscard]] auto update(std::span<const std::byte> rgba8) noexcept -> result<void>;

    [[nodiscard]] auto update(const texture_data& data) noexcept -> result<void>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_texture(const device& device, u32 width_px, u32 height_px,
                               const texture_data& data) noexcept -> result<texture>;

    texture(const device* device, void* image, void* memory, void* view, void* sampler,
            bool initialized, u32 width_px, u32 height_px, texture_format format) noexcept
        : device_(device), image_(image), memory_(memory), view_(view), sampler_(sampler),
          initialized_(initialized), width_px_(width_px), height_px_(height_px), format_(format) {
    }

    const device* device_ = nullptr;
    mutable void* image_ = nullptr;
    mutable void* memory_ = nullptr;
    mutable void* view_ = nullptr;
    mutable void* sampler_ = nullptr;
    bool initialized_ = false;
    u32 width_px_ = 0;
    u32 height_px_ = 0;
    texture_format format_ = texture_format::rgba8;
};

[[nodiscard]] auto create_texture(const device& device, u32 width_px, u32 height_px,
                                  const texture_data& data) noexcept -> result<texture>;

[[nodiscard]] auto create_texture(const device& device, u32 width_px, u32 height_px,
                                  std::span<const std::byte> rgba8) noexcept -> result<texture>;

}
