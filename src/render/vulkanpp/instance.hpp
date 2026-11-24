// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <string>

namespace pwm::vulkanpp {

struct application_info {
    std::string name = "powerwm";
    u32 version = 0;
    std::string engine_name = "powerwm";
    u32 engine_version = 0;
    u32 api_version = 0;
};

class instance {
  public:
    instance() noexcept = default;

    instance(const instance&) = delete;
    auto operator=(const instance&) -> instance& = delete;

    instance(instance&&) noexcept = default;
    auto operator=(instance&&) noexcept -> instance& = default;

    [[nodiscard]] static auto create(const application_info& info) noexcept -> result<instance>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    auto destroy() const noexcept -> void;

  private:
    explicit instance(void* handle) noexcept : handle_(handle) {
    }

    mutable void* handle_ = nullptr;
};

}