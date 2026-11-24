// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/types.hpp>

#include <span>
#include <vector>

namespace pwm::scene {

using node_id = u64;
using texture_id = u64;

struct color {
    f32 red = 0.0F;
    f32 green = 0.0F;
    f32 blue = 0.0F;
    f32 alpha = 1.0F;
};

struct node_style {
    color fill{};
    color shadow{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.45F};
    texture_id texture = 0;
    f32 corner_radius = 0.0F;
    f32 shadow_radius = 0.0F;
    f32 shadow_offset_y = 0.0F;
    f32 blur_radius = 0.0F;
    f32 rotation = 0.0F;
    f32 scale = 1.0F;
    bool clip_children = false;
};

class node {
  public:
    node() noexcept = default;

    explicit node(node_id id) noexcept : id_(id) {
    }

    [[nodiscard]] auto id() const noexcept -> node_id;

    [[nodiscard]] auto parent() const noexcept -> node*;

    [[nodiscard]] auto children() const noexcept -> std::span<node* const>;

    [[nodiscard]] auto add_child(node& child) noexcept -> bool;

    [[nodiscard]] auto remove_child(node& child) noexcept -> bool;

    [[nodiscard]] auto bounds() const noexcept -> base::recti;

    auto set_bounds(base::recti value) noexcept -> void;

    [[nodiscard]] auto visible() const noexcept -> bool;

    auto set_visible(bool value) noexcept -> void;

    [[nodiscard]] auto opacity() const noexcept -> f32;

    auto set_opacity(f32 value) noexcept -> void;

    [[nodiscard]] auto style() const noexcept -> const node_style&;

    auto set_style(const node_style& value) noexcept -> void;

    auto set_texture(texture_id value) noexcept -> void;

    auto invalidate(base::recti value) -> void;

    [[nodiscard]] auto take_damage() -> base::region;

  private:
    node_id id_ = 0;
    node* parent_ = nullptr;
    std::vector<node*> children_;
    base::recti bounds_{};
    bool visible_ = true;
    f32 opacity_ = 1.0F;
    node_style style_{};
    base::region damage_;
};

class transaction {
  public:
    transaction() = default;

    auto move(node& target, base::recti bounds) -> void;

    auto show(node& target, bool visible) -> void;

    [[nodiscard]] auto damage() const noexcept -> base::region;

    auto commit() noexcept -> base::region;

  private:
    struct change {
        node* target = nullptr;
        base::recti old_bounds{};
        base::recti new_bounds{};
        bool old_visible = false;
        bool new_visible = false;
    };

    auto find(node& target) -> change*;

    std::vector<change> changes_;
    bool committed_ = false;
};

class scene {
  public:
    scene() noexcept = default;

    [[nodiscard]] auto root() noexcept -> node&;

    [[nodiscard]] auto begin() noexcept -> transaction;

    auto add_damage(base::recti value) -> void;

    auto add_damage(const base::region& value) -> void;

    [[nodiscard]] auto damage() const noexcept -> const base::region&;

    [[nodiscard]] auto take_damage() -> base::region;

    [[nodiscard]] auto collect_damage() -> base::region;

  private:
    auto collect_node_damage(node& target, base::region& result) -> void;

    node root_{0};
    base::region damage_;
};

}
