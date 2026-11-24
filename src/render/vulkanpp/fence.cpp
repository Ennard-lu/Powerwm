// SPDX-License-Identifier: Apache-2.0

#include <render/vulkanpp/fence.hpp>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

auto create_fence(const device& device, bool signaled) noexcept -> result<fence> {
    VkFenceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    create_info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    VkFence handle = VK_NULL_HANDLE;
    const VkResult result = vkCreateFence(reinterpret_cast<VkDevice>(device.native_handle()),
                                          &create_info, nullptr, &handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateFence failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return fence{device.native_handle(), handle};
}

auto fence::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto fence::native_handle() const noexcept -> void* {
    return handle_;
}

auto fence::wait(u64 timeout_ns) noexcept -> result<void> {
    const VkFence fence_handle = reinterpret_cast<VkFence>(handle_);
    const VkResult result =
        vkWaitForFences(reinterpret_cast<VkDevice>(device_), 1, &fence_handle, VK_TRUE, timeout_ns);
    if (result == VK_TIMEOUT) {
        return std::unexpected(make_error(error_code::io, "vkWaitForFences timed out"));
    }
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkWaitForFences failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

auto fence::reset() noexcept -> result<void> {
    const VkFence fence_handle = reinterpret_cast<VkFence>(handle_);
    const VkResult result = vkResetFences(reinterpret_cast<VkDevice>(device_), 1, &fence_handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkResetFences failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

auto fence::destroy() const noexcept -> void {
    vkDestroyFence(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkFence>(handle_),
                   nullptr);
    handle_ = nullptr;
}

}