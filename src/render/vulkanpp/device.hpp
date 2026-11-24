// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/instance.hpp>

#include <string>
#include <utility>

namespace pwm::vulkanpp {

struct device_info {
    std::string name;
    u32 vendor_id = 0;
    u32 device_id = 0;
    u32 api_version = 0;
    u32 driver_version = 0;
    u32 queue_family_index = 0;
};

class device {
  public:
    device() noexcept = default;

    device(const device&) = delete;
    auto operator=(const device&) -> device& = delete;

    device(device&&) noexcept = default;
    auto operator=(device&&) noexcept -> device& = default;

    [[nodiscard]] static auto create(const instance& instance) noexcept -> result<device>;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto info() const noexcept -> const device_info&;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    [[nodiscard]] auto native_queue() const noexcept -> void*;

    [[nodiscard]] auto native_physical() const noexcept -> void*;

    [[nodiscard]] auto queue_family_index() const noexcept -> u32;

    [[nodiscard]] auto find_memory_type(u32 type_bits, u32 required_flags) const noexcept
        -> result<u32>;

    [[nodiscard]] auto supports_dma_buf() const noexcept -> bool;

    auto destroy() const noexcept -> void;

  private:
    device(void* handle, void* queue, void* physical, device_info info,
           bool supports_dma_buf) noexcept
        : handle_(handle), queue_(queue), physical_(physical), info_(std::move(info)),
          supports_dma_buf_(supports_dma_buf) {
    }

    mutable void* handle_ = nullptr;
    mutable void* queue_ = nullptr;
    mutable void* physical_ = nullptr;
    device_info info_{};
    bool supports_dma_buf_ = false;
};

}