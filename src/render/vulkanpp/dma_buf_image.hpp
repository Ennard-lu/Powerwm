// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/color.hpp>
#include <render/vulkanpp/device.hpp>

#include <cstddef>
#include <span>

namespace pwm::vulkanpp {

class dma_buf_image {
  public:
    dma_buf_image() noexcept = default;

    dma_buf_image(const dma_buf_image&) = delete;
    auto operator=(const dma_buf_image&) -> dma_buf_image& = delete;

    dma_buf_image(dma_buf_image&&) noexcept = default;
    auto operator=(dma_buf_image&&) noexcept -> dma_buf_image& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto width_px() const noexcept -> u32;

    [[nodiscard]] auto height_px() const noexcept -> u32;

    [[nodiscard]] auto pitch_bytes() const noexcept -> u32;

    [[nodiscard]] auto fd() const noexcept -> handle_t;

    [[nodiscard]] auto clear(color clear_color) noexcept -> result<void>;

    [[nodiscard]] auto render_triangle(std::span<const std::byte> vertex_spirv,
                                       std::span<const std::byte> fragment_spirv) noexcept
        -> result<void>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_dma_buf_image(const device& device, u32 width_px, u32 height_px) noexcept
        -> result<dma_buf_image>;

    dma_buf_image(const device* device, void* image, void* memory, handle_t fd, u32 width_px,
                  u32 height_px, u32 pitch_bytes) noexcept
        : device_(device), image_(image), memory_(memory), fd_(fd), width_px_(width_px),
          height_px_(height_px), pitch_bytes_(pitch_bytes) {
    }

    const device* device_ = nullptr;
    mutable void* image_ = nullptr;
    mutable void* memory_ = nullptr;
    mutable handle_t fd_ = -1;
    u32 width_px_ = 0;
    u32 height_px_ = 0;
    u32 pitch_bytes_ = 0;
};

[[nodiscard]] auto create_dma_buf_image(const device& device, u32 width_px, u32 height_px) noexcept
    -> result<dma_buf_image>;

}