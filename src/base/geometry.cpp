// SPDX-License-Identifier: Apache-2.0

#include <base/geometry.hpp>

#include <limits>

namespace pwm::base {

auto rect::intersection(const rect& other) const noexcept -> rect {
    const f32 left = std::max(x, other.x);
    const f32 top = std::max(y, other.y);
    const f32 right_edge = std::min(right(), other.right());
    const f32 bottom_edge = std::min(bottom(), other.bottom());
    return {left, top, std::max(0.0F, right_edge - left), std::max(0.0F, bottom_edge - top)};
}

auto rect::united(const rect& other) const noexcept -> rect {
    const f32 left = std::min(x, other.x);
    const f32 top = std::min(y, other.y);
    const f32 right_edge = std::max(right(), other.right());
    const f32 bottom_edge = std::max(bottom(), other.bottom());
    return {left, top, right_edge - left, bottom_edge - top};
}

auto recti::intersection(const recti& other) const noexcept -> recti {
    const i32 left = std::max(x, other.x);
    const i32 top = std::max(y, other.y);
    const i32 right_edge = std::min(right(), other.right());
    const i32 bottom_edge = std::min(bottom(), other.bottom());
    return {left, top, std::max(0, right_edge - left), std::max(0, bottom_edge - top)};
}

auto recti::united(const recti& other) const noexcept -> recti {
    const i32 left = std::min(x, other.x);
    const i32 top = std::min(y, other.y);
    const i32 right_edge = std::max(right(), other.right());
    const i32 bottom_edge = std::max(bottom(), other.bottom());
    return {left, top, right_edge - left, bottom_edge - top};
}

auto region::add(recti value) -> void {
    if (value.is_empty()) {
        return;
    }

    usize index = 0;
    while (index < rects_.size()) {
        if (rects_[index].intersects(value)) {
            value = rects_[index].united(value);
            rects_.erase(rects_.begin() + static_cast<std::ptrdiff_t>(index));
            index = 0;
            continue;
        }
        ++index;
    }
    rects_.push_back(value);
}

auto region::clear() noexcept -> void {
    rects_.clear();
}

auto region::empty() const noexcept -> bool {
    return rects_.empty();
}

auto region::size() const noexcept -> usize {
    return rects_.size();
}

auto region::bounds() const noexcept -> recti {
    if (rects_.empty()) {
        return {};
    }
    recti result = rects_.front();
    for (const auto& value : rects_) {
        result = result.united(value);
    }
    return result;
}

auto region::contains(recti value) const noexcept -> bool {
    if (value.is_empty()) {
        return true;
    }
    for (const auto& candidate : rects_) {
        if (candidate.contains(value)) {
            return true;
        }
    }
    return false;
}

auto region::rects() const noexcept -> std::span<const recti> {
    return rects_;
}

auto region::intersection(const recti& clip) const -> region {
    region result;
    for (const auto& candidate : rects_) {
        const recti clipped = candidate.intersection(clip);
        if (!clipped.is_empty()) {
            result.add(clipped);
        }
    }
    return result;
}

}