// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/scene_renderer.hpp>
#include <render/vulkanpp/texture.hpp>
#include <scene/scene.hpp>
#include <wayland/core.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace pwm::wayland {

class surface_scene {
  public:
    surface_scene(const vulkanpp::device& device, u32 output_width, u32 output_height) noexcept;

    [[nodiscard]] auto on_commit(const surface_commit_event& event) -> result<void>;

    [[nodiscard]] auto on_commit(u64 surface_key, const surface_commit_event& event)
        -> result<void>;

    [[nodiscard]] auto set_surface_bounds(u64 surface_key, base::recti bounds) -> result<void>;

    [[nodiscard]] auto surface_bounds(u64 surface_key) const noexcept -> std::optional<base::recti>;

    [[nodiscard]] auto hit_test(f64 x, f64 y) const noexcept -> std::optional<u64>;

    [[nodiscard]] auto scene_graph() noexcept -> scene::scene&;

    [[nodiscard]] auto textures() const noexcept -> std::span<const render::texture_binding>;

  private:
    struct surface_entry {
        u64 surface = 0;
        scene::node node;
        std::unique_ptr<vulkanpp::texture> texture;
        scene::texture_id texture_id = 0;
        object_id buffer = 0;
        base::recti bounds{};
    };

    auto find_surface(u64 surface) noexcept -> surface_entry*;
    auto ensure_surface(u64 surface) -> surface_entry&;
    auto rebuild_bindings() -> void;
    [[nodiscard]] auto next_cascade() noexcept -> base::recti;

    const vulkanpp::device* device_;
    scene::scene graph_;
    scene::node background_;
    scene::texture_id next_texture_id_ = 1;
    std::vector<std::unique_ptr<surface_entry>> surfaces_;
    std::vector<render::texture_binding> bindings_;
};

}
