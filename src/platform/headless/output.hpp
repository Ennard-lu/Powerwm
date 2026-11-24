// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

namespace pwm::platform::headless {

class output {
  public:
    output() noexcept = default;

    [[nodiscard]] static auto create(u32 width_px, u32 height_px) noexcept -> result<output>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto width_px() const noexcept -> u32;

    [[nodiscard]] auto height_px() const noexcept -> u32;

    [[nodiscard]] auto commit() noexcept -> result<void>;

    [[nodiscard]] auto wait_for_page_flip() noexcept -> result<void>;

    [[nodiscard]] auto commit_count() const noexcept -> u64;

    [[nodiscard]] auto flip_count() const noexcept -> u64;

    auto destroy() const noexcept -> void;

  private:
    mutable bool valid_ = false;
    mutable u32 width_px_ = 0;
    mutable u32 height_px_ = 0;
    mutable u64 commit_count_ = 0;
    mutable u64 flip_count_ = 0;
};

}