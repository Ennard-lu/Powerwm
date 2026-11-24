// SPDX-License-Identifier: Apache-2.0

#include <render/vulkanpp/command_pool.hpp>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

auto create_command_pool(const device& device) noexcept -> result<command_pool> {
    VkCommandPoolCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    create_info.queueFamilyIndex = device.queue_family_index();

    VkCommandPool handle = VK_NULL_HANDLE;
    const VkResult result = vkCreateCommandPool(reinterpret_cast<VkDevice>(device.native_handle()),
                                                &create_info, nullptr, &handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateCommandPool failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return command_pool{device.native_handle(), handle, device.queue_family_index()};
}

auto command_pool::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto command_pool::native_handle() const noexcept -> void* {
    return handle_;
}

auto command_pool::allocate() noexcept -> result<command_buffer> {

    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = reinterpret_cast<VkCommandPool>(handle_);
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer_handle = VK_NULL_HANDLE;
    const VkResult result = vkAllocateCommandBuffers(reinterpret_cast<VkDevice>(device_),
                                                     &allocate_info, &command_buffer_handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkAllocateCommandBuffers failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return command_buffer{device_, handle_, command_buffer_handle};
}

auto command_pool::destroy() const noexcept -> void {
    vkDestroyCommandPool(reinterpret_cast<VkDevice>(device_),
                         reinterpret_cast<VkCommandPool>(handle_), nullptr);
    handle_ = nullptr;
}

auto command_buffer::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto command_buffer::native_handle() const noexcept -> void* {
    return handle_;
}

auto command_buffer::begin() noexcept -> result<void> {
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult result =
        vkBeginCommandBuffer(reinterpret_cast<VkCommandBuffer>(handle_), &begin_info);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkBeginCommandBuffer failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

auto command_buffer::end() noexcept -> result<void> {
    const VkResult result = vkEndCommandBuffer(reinterpret_cast<VkCommandBuffer>(handle_));
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkEndCommandBuffer failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

auto command_buffer::destroy() const noexcept -> void {
    const VkCommandBuffer command_buffer_handle = reinterpret_cast<VkCommandBuffer>(handle_);
    vkFreeCommandBuffers(reinterpret_cast<VkDevice>(device_),
                         reinterpret_cast<VkCommandPool>(pool_), 1, &command_buffer_handle);
    handle_ = nullptr;
}

auto submit(const device& device, const command_buffer& command_buffer, fence& fence) noexcept
    -> result<void> {
    const VkCommandBuffer handle =
        reinterpret_cast<VkCommandBuffer>(command_buffer.native_handle());
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &handle;

    const VkResult result =
        vkQueueSubmit(reinterpret_cast<VkQueue>(device.native_queue()), 1, &submit_info,
                      reinterpret_cast<VkFence>(fence.native_handle()));
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkQueueSubmit failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return {};
}

}