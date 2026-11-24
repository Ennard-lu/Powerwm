// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/device.hpp>

namespace pwm::vulkanpp {

class fence {
  public:
    fence() noexcept = default;

    fence(const fence&) = delete;
    auto operator=(const fence&) -> fence& = delete;

    fence(fence&&) noexcept = default;
    auto operator=(fence&&) noexcept -> fence& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    [[nodiscard]] auto wait(u64 timeout_ns = static_cast<u64>(-1)) noexcept -> result<void>;

    [[nodiscard]] auto reset() noexcept -> result<void>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_fence(const device& device, bool signaled) noexcept -> result<fence>;

    fence(void* device, void* handle) noexcept : device_(device), handle_(handle) {
    }

    mutable void* device_ = nullptr;
    mutable void* handle_ = nullptr;
};

[[nodiscard]] auto create_fence(const device& device, bool signaled) noexcept -> result<fence>;

}