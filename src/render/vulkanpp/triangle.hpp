// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/color.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/image.hpp>

#include <cstddef>
#include <span>
#include <utility>

namespace pwm::vulkanpp {

class triangle_target {
  public:
    triangle_target() noexcept = default;

    triangle_target(const triangle_target&) = delete;
    auto operator=(const triangle_target&) -> triangle_target& = delete;

    triangle_target(triangle_target&&) noexcept = default;
    auto operator=(triangle_target&&) noexcept -> triangle_target& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto width_px() const noexcept -> u32;

    [[nodiscard]] auto height_px() const noexcept -> u32;

    [[nodiscard]] auto render() noexcept -> result<void>;

    [[nodiscard]] auto read_pixel(u32 x, u32 y) const noexcept -> result<color>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_triangle_target(const device& device, u32 width_px, u32 height_px,
                                       std::span<const std::byte> vertex_spirv,
                                       std::span<const std::byte> fragment_spirv) noexcept
        -> result<triangle_target>;

    triangle_target(const device* device, image image, buffer readback, command_pool pool,
                    command_buffer command_buffer, fence fence, void* view, void* render_pass,
                    void* pipeline_layout, void* pipeline, void* framebuffer, u32 width_px,
                    u32 height_px) noexcept
        : device_(device), image_(std::move(image)), readback_(std::move(readback)),
          pool_(std::move(pool)), command_buffer_(std::move(command_buffer)),
          fence_(std::move(fence)), view_(view), render_pass_(render_pass),
          pipeline_layout_(pipeline_layout), pipeline_(pipeline), framebuffer_(framebuffer),
          width_px_(width_px), height_px_(height_px) {
    }

    const device* device_ = nullptr;
    image image_;
    buffer readback_;
    command_pool pool_;
    command_buffer command_buffer_;
    fence fence_;
    mutable void* view_ = nullptr;
    mutable void* render_pass_ = nullptr;
    mutable void* pipeline_layout_ = nullptr;
    mutable void* pipeline_ = nullptr;
    mutable void* framebuffer_ = nullptr;
    u32 width_px_ = 0;
    u32 height_px_ = 0;
};

[[nodiscard]] auto create_triangle_target(const device& device, u32 width_px, u32 height_px,
                                          std::span<const std::byte> vertex_spirv,
                                          std::span<const std::byte> fragment_spirv) noexcept
    -> result<triangle_target>;

[[nodiscard]] auto render_triangle_to_image(const device& device, void* native_image, u32 width_px,
                                            u32 height_px, std::span<const std::byte> vertex_spirv,
                                            std::span<const std::byte> fragment_spirv) noexcept
    -> result<void>;

}