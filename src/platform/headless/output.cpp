// SPDX-License-Identifier: Apache-2.0

#include <platform/headless/output.hpp>

namespace pwm::platform::headless {

auto output::create(u32 width_px, u32 height_px) noexcept -> result<output> {
    if (width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "headless output size must be non-zero"));
    }

    output result{};
    result.valid_ = true;
    result.width_px_ = width_px;
    result.height_px_ = height_px;
    return result;
}

auto output::valid() const noexcept -> bool {
    return valid_;
}

auto output::width_px() const noexcept -> u32 {
    return width_px_;
}

auto output::height_px() const noexcept -> u32 {
    return height_px_;
}

auto output::commit() noexcept -> result<void> {
    if (!valid_) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "headless output is invalid"));
    }
    ++commit_count_;
    return {};
}

auto output::wait_for_page_flip() noexcept -> result<void> {
    if (!valid_) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "headless output is invalid"));
    }
    ++flip_count_;
    return {};
}

auto output::commit_count() const noexcept -> u64 {
    return commit_count_;
}

auto output::flip_count() const noexcept -> u64 {
    return flip_count_;
}

auto output::destroy() const noexcept -> void {
    valid_ = false;
    width_px_ = 0;
    height_px_ = 0;
}

}