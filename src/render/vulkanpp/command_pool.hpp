// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/fence.hpp>

namespace pwm::vulkanpp {

class command_buffer {
  public:
    command_buffer() noexcept = default;

    command_buffer(const command_buffer&) = delete;
    auto operator=(const command_buffer&) -> command_buffer& = delete;

    command_buffer(command_buffer&&) noexcept = default;
    auto operator=(command_buffer&&) noexcept -> command_buffer& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    [[nodiscard]] auto begin() noexcept -> result<void>;

    [[nodiscard]] auto end() noexcept -> result<void>;

    auto destroy() const noexcept -> void;

  private:
    friend class command_pool;

    command_buffer(void* device, void* pool, void* handle) noexcept
        : device_(device), pool_(pool), handle_(handle) {
    }

    mutable void* device_ = nullptr;
    mutable void* pool_ = nullptr;
    mutable void* handle_ = nullptr;
};

class command_pool {
  public:
    command_pool() noexcept = default;

    command_pool(const command_pool&) = delete;
    auto operator=(const command_pool&) -> command_pool& = delete;

    command_pool(command_pool&&) noexcept = default;
    auto operator=(command_pool&&) noexcept -> command_pool& = default;

    [[nodiscard]] auto valid() const noexcept -> bool;

    [[nodiscard]] auto native_handle() const noexcept -> void*;

    [[nodiscard]] auto allocate() noexcept -> result<command_buffer>;

    auto destroy() const noexcept -> void;

  private:
    friend auto create_command_pool(const device& device) noexcept -> result<command_pool>;

    command_pool(void* device, void* handle, u32 queue_family) noexcept
        : device_(device), handle_(handle), queue_family_(queue_family) {
    }

    mutable void* device_ = nullptr;
    mutable void* handle_ = nullptr;
    u32 queue_family_ = 0;
};

[[nodiscard]] auto create_command_pool(const device& device) noexcept -> result<command_pool>;

[[nodiscard]] auto submit(const device& device, const command_buffer& command_buffer,
                          fence& fence) noexcept -> result<void>;

}