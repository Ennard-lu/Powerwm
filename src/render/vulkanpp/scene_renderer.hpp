// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/color.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/texture.hpp>
#include <scene/scene.hpp>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace pwm::render {

enum class target_format {
    rgba8,
    bgra8,
};

struct texture_binding {
    scene::texture_id id = 0;
    const vulkanpp::texture* texture = nullptr;
};

struct frame_statistics {
    f64 cpu_ms = 0.0;
    u32 draw_calls = 0;
    u32 quads = 0;
    bool full_redraw = true;
};

class scene_renderer {
  public:
    scene_renderer() noexcept = default;

    scene_renderer(const scene_renderer&) = delete;
    auto operator=(const scene_renderer&) -> scene_renderer& = delete;

    scene_renderer(scene_renderer&&) noexcept = default;
    auto operator=(scene_renderer&&) noexcept -> scene_renderer& = default;

    [[nodiscard]] static auto create(const vulkanpp::device& device, target_format format,
                                     std::span<const std::byte> vertex_spirv,
                                     std::span<const std::byte> fragment_spirv) noexcept
        -> result<scene_renderer>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto render(void* native_image, u32 width_px, u32 height_px,
                              const scene::node& root, std::span<const texture_binding> textures,
                              const base::region& damage, vulkanpp::color clear_color,
                              base::point origin = {}) noexcept -> result<void>;

    [[nodiscard]] auto statistics() const noexcept -> const frame_statistics&;

    auto destroy() noexcept -> void;

    struct target_entry {
        void* image = nullptr;
        u32 width_px = 0;
        u32 height_px = 0;
        void* view = nullptr;
        void* framebuffer_clear = nullptr;
        void* framebuffer_load = nullptr;
    };

    scene_renderer(const vulkanpp::device* device, void* render_pass_clear, void* render_pass_load,
                   void* pipeline_layout, void* pipeline, void* descriptor_layout,
                   void* descriptor_pool, void* descriptor_set, vulkanpp::texture fallback_texture,
                   vulkanpp::command_pool pool, vulkanpp::command_buffer command,
                   vulkanpp::fence fence, target_format format, u32 width_px,
                   u32 height_px) noexcept
        : device_(device), render_pass_clear_(render_pass_clear),
          render_pass_load_(render_pass_load), pipeline_layout_(pipeline_layout),
          pipeline_(pipeline), descriptor_layout_(descriptor_layout),
          descriptor_pool_(descriptor_pool), descriptor_set_(descriptor_set),
          fallback_texture_(std::move(fallback_texture)), pool_(std::move(pool)),
          command_(std::move(command)), fence_(std::move(fence)), format_(format),
          width_px_(width_px), height_px_(height_px) {
        targets_.reserve(8);
        active_bindings_.reserve(64);
    }

    auto find_target(void* image, u32 width_px, u32 height_px) noexcept -> result<target_entry*>;
    auto create_target(void* image, u32 width_px, u32 height_px) noexcept -> result<target_entry*>;
    auto update_descriptors(std::span<const texture_binding> textures) noexcept -> void;
    auto record_clears(void* command, const base::region& damage,
                       vulkanpp::color clear_color) noexcept -> void;
    auto record_draws(void* command, const scene::node& root, bool full_redraw,
                      const base::region& damage) noexcept -> void;
    auto record_pass(void* command, const target_entry& target, bool full_redraw,
                     const base::region& damage, const scene::node& root,
                     vulkanpp::color clear_color) noexcept -> void;
    auto draw_node(const scene::node& target, base::recti clip, f32 opacity,
                   const base::recti* damage_rect) noexcept -> void;
    auto draw_quad(const scene::node& target, base::recti clip, f32 opacity, bool shadow) noexcept
        -> void;
    auto set_scissor(base::recti rect) noexcept -> bool;

    const vulkanpp::device* device_ = nullptr;
    mutable void* render_pass_clear_ = nullptr;
    mutable void* render_pass_load_ = nullptr;
    mutable void* pipeline_layout_ = nullptr;
    mutable void* pipeline_ = nullptr;
    mutable void* descriptor_layout_ = nullptr;
    mutable void* descriptor_pool_ = nullptr;
    mutable void* descriptor_set_ = nullptr;
    vulkanpp::texture fallback_texture_;
    vulkanpp::command_pool pool_;
    vulkanpp::command_buffer command_;
    vulkanpp::fence fence_;
    target_format format_ = target_format::rgba8;
    u32 width_px_ = 0;
    u32 height_px_ = 0;
    std::vector<target_entry> targets_;
    std::vector<const texture_binding*> active_bindings_;
    frame_statistics statistics_{};
    base::point origin_{};
};

}
