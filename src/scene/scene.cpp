// SPDX-License-Identifier: Apache-2.0

#include <scene/scene.hpp>

#include <algorithm>

namespace pwm::scene {

auto node::id() const noexcept -> node_id {
    return id_;
}

auto node::parent() const noexcept -> node* {
    return parent_;
}

auto node::children() const noexcept -> std::span<node* const> {
    return children_;
}

auto node::add_child(node& child) noexcept -> bool {
    if (&child == this || child.parent_ != nullptr || child.id_ == id_) {
        return false;
    }
    child.parent_ = this;
    children_.push_back(&child);
    return true;
}

auto node::remove_child(node& child) noexcept -> bool {
    const auto iterator = std::find(children_.begin(), children_.end(), &child);
    if (iterator == children_.end()) {
        return false;
    }
    children_.erase(iterator);
    child.parent_ = nullptr;
    return true;
}

auto node::bounds() const noexcept -> base::recti {
    return bounds_;
}

auto node::set_bounds(base::recti value) noexcept -> void {
    bounds_ = value;
}

auto node::visible() const noexcept -> bool {
    return visible_;
}

auto node::set_visible(bool value) noexcept -> void {
    visible_ = value;
}

auto node::opacity() const noexcept -> f32 {
    return opacity_;
}

auto node::set_opacity(f32 value) noexcept -> void {
    opacity_ = std::clamp(value, 0.0F, 1.0F);
}

auto transaction::find(node& target) -> change* {
    for (auto& entry : changes_) {
        if (entry.target == &target) {
            return &entry;
        }
    }
    return nullptr;
}

auto transaction::move(node& target, base::recti bounds) -> void {
    if (change* existing = find(target); existing != nullptr) {
        existing->new_bounds = bounds;
        return;
    }
    changes_.push_back({
        .target = &target,
        .old_bounds = target.bounds(),
        .new_bounds = bounds,
        .old_visible = target.visible(),
        .new_visible = target.visible(),
    });
}

auto transaction::show(node& target, bool visible) -> void {
    if (change* existing = find(target); existing != nullptr) {
        existing->new_visible = visible;
        return;
    }
    changes_.push_back({
        .target = &target,
        .old_bounds = target.bounds(),
        .new_bounds = target.bounds(),
        .old_visible = target.visible(),
        .new_visible = visible,
    });
}

auto transaction::damage() const noexcept -> base::region {
    base::region result;
    for (const auto& entry : changes_) {
        if (entry.old_visible) {
            result.add(entry.old_bounds);
        }
        if (entry.new_visible) {
            result.add(entry.new_bounds);
        }
    }
    return result;
}

auto transaction::commit() noexcept -> base::region {
    if (committed_) {
        return {};
    }

    base::region result = damage();
    for (const auto& entry : changes_) {
        entry.target->set_bounds(entry.new_bounds);
        entry.target->set_visible(entry.new_visible);
    }
    changes_.clear();
    committed_ = true;
    return result;
}

auto scene::root() noexcept -> node& {
    return root_;
}

auto scene::begin() noexcept -> transaction {
    return {};
}

}