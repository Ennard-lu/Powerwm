// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/geometry.hpp>
#include <base/types.hpp>

#include <span>
#include <vector>

namespace pwm::scene {

using node_id = u64;

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

  private:
    node_id id_ = 0;
    node* parent_ = nullptr;
    std::vector<node*> children_;
    base::recti bounds_{};
    bool visible_ = true;
    f32 opacity_ = 1.0F;
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

  private:
    node root_{0};
};

}