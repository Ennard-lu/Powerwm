// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/types.hpp>

#include <algorithm>
#include <span>
#include <vector>

namespace pwm::base {

struct point {
    f32 x = 0.0F;
    f32 y = 0.0F;
};

struct size {
    f32 width = 0.0F;
    f32 height = 0.0F;
};

struct rect {
    f32 x = 0.0F;
    f32 y = 0.0F;
    f32 width = 0.0F;
    f32 height = 0.0F;

    [[nodiscard]] auto right() const noexcept -> f32 {
        return x + width;
    }

    [[nodiscard]] auto bottom() const noexcept -> f32 {
        return y + height;
    }

    [[nodiscard]] auto is_empty() const noexcept -> bool {
        return width <= 0.0F || height <= 0.0F;
    }

    [[nodiscard]] auto contains(point value) const noexcept -> bool {
        return value.x >= x && value.x < right() && value.y >= y && value.y < bottom();
    }

    [[nodiscard]] auto intersects(const rect& other) const noexcept -> bool {
        return x < other.right() && right() > other.x && y < other.bottom() && bottom() > other.y;
    }

    [[nodiscard]] auto intersection(const rect& other) const noexcept -> rect;
    [[nodiscard]] auto united(const rect& other) const noexcept -> rect;
};

struct recti {
    i32 x = 0;
    i32 y = 0;
    i32 width = 0;
    i32 height = 0;

    [[nodiscard]] auto right() const noexcept -> i32 {
        return x + width;
    }

    [[nodiscard]] auto bottom() const noexcept -> i32 {
        return y + height;
    }

    [[nodiscard]] auto is_empty() const noexcept -> bool {
        return width <= 0 || height <= 0;
    }

    [[nodiscard]] auto contains(recti other) const noexcept -> bool {
        return other.x >= x && other.y >= y && other.right() <= right() &&
               other.bottom() <= bottom();
    }

    [[nodiscard]] auto intersects(const recti& other) const noexcept -> bool {
        return x < other.right() && right() > other.x && y < other.bottom() && bottom() > other.y;
    }

    [[nodiscard]] auto intersection(const recti& other) const noexcept -> recti;
    [[nodiscard]] auto united(const recti& other) const noexcept -> recti;

    [[nodiscard]] auto operator==(const recti&) const noexcept -> bool = default;
};

class region {
  public:
    region() = default;

    auto add(recti value) -> void;
    auto clear() noexcept -> void;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> usize;
    [[nodiscard]] auto bounds() const noexcept -> recti;
    [[nodiscard]] auto contains(recti value) const noexcept -> bool;
    [[nodiscard]] auto rects() const noexcept -> std::span<const recti>;
    [[nodiscard]] auto intersection(const recti& clip) const -> region;

  private:
    std::vector<recti> rects_;
};

}