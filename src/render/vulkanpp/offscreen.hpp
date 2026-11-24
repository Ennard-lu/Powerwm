// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/color.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/image.hpp>

#include <utility>

namespace pwm::vulkanpp {

class offscreen_target {
  public:
    offscreen_target() noexcept = default;

    offscreen_target(const offscreen_target&) = delete;
    auto operator=(const offscreen_target&) -> offscreen_target& = delete;

    offscreen_target(offscreen_target&&) noexcept = default;
    auto operator=(offscreen_target&&) noexcept -> offscreen_target& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto width_px() const noexcept -> u32;

    [[nodiscard]] auto height_px() const noexcept -> u32;

    [[nodiscard]] auto render(color clear_color) noexcept -> result<void>;

    [[nodiscard]] auto read_pixel(u32 x, u32 y) const noexcept -> result<color>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_offscreen_target(const device& device, u32 width_px, u32 height_px) noexcept
        -> result<offscreen_target>;

    offscreen_target(void* device, void* queue, image image, buffer buffer, command_pool pool,
                     command_buffer command_buffer, fence fence, u32 width_px,
                     u32 height_px) noexcept
        : device_(device), queue_(queue), image_(std::move(image)), buffer_(std::move(buffer)),
          pool_(std::move(pool)), command_buffer_(std::move(command_buffer)),
          fence_(std::move(fence)), width_px_(width_px), height_px_(height_px) {
    }

    mutable void* device_ = nullptr;
    mutable void* queue_ = nullptr;
    image image_;
    buffer buffer_;
    command_pool pool_;
    command_buffer command_buffer_;
    fence fence_;
    u32 width_px_ = 0;
    u32 height_px_ = 0;
};

[[nodiscard]] auto create_offscreen_target(const device& device, u32 width_px,
                                           u32 height_px) noexcept -> result<offscreen_target>;

}