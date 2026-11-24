// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/fence.hpp>

#include <unistd.h>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

auto record_layout_transition(VkCommandBuffer command_buffer, VkImage image,
                              VkImageLayout old_layout, VkImageLayout new_layout,
                              VkAccessFlags source_access, VkAccessFlags destination_access,
                              VkPipelineStageFlags source_stage,
                              VkPipelineStageFlags destination_stage) -> void {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    barrier.srcAccessMask = source_access;
    barrier.dstAccessMask = destination_access;

    vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

auto record_clear(VkCommandBuffer command_buffer, VkImage image, color clear_color) -> void {
    VkClearColorValue value{};
    value.float32[0] = clear_color.red;
    value.float32[1] = clear_color.green;
    value.float32[2] = clear_color.blue;
    value.float32[3] = clear_color.alpha;

    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    vkCmdClearColorImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &value, 1,
                         &range);
}

}

auto create_dma_buf_image(const device& device, u32 width_px, u32 height_px) noexcept
    -> result<dma_buf_image> {
    if (!device.supports_dma_buf()) {
        return std::unexpected(
            make_error(error_code::unsupported, "Vulkan DMA-BUF export is unavailable"));
    }
    if (width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "DMA-BUF image size must be non-zero"));
    }

    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent = {width_px, height_px, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    VkImage image = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(native_device, &image_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImage failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(native_device, image, &requirements);
    auto memory_type =
        device.find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(memory_type.error());
    }

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.image = image;

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.pNext = &dedicated_info;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = &export_info;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(native_device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    result = vkBindImageMemory(native_device, image, memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindImageMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    const auto get_memory_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(native_device, "vkGetMemoryFdKHR"));
    if (get_memory_fd == nullptr) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(
            make_error(error_code::unsupported, "vkGetMemoryFdKHR is unavailable"));
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    handle_t fd = -1;
    result = get_memory_fd(native_device, &fd_info, &fd);
    if (result != VK_SUCCESS || fd < 0) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(make_error(error_code::io, "vkGetMemoryFdKHR failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = 0;
    subresource.arrayLayer = 0;

    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(native_device, image, &subresource, &layout);

    return dma_buf_image{
        &device, image, memory, fd, width_px, height_px, static_cast<u32>(layout.rowPitch)};
}

auto dma_buf_image::valid() const noexcept -> bool {
    return image_ != nullptr;
}

auto dma_buf_image::width_px() const noexcept -> u32 {
    return width_px_;
}

auto dma_buf_image::height_px() const noexcept -> u32 {
    return height_px_;
}

auto dma_buf_image::pitch_bytes() const noexcept -> u32 {
    return pitch_bytes_;
}

auto dma_buf_image::fd() const noexcept -> handle_t {
    return fd_;
}

auto dma_buf_image::clear(color clear_color) noexcept -> result<void> {
    auto pool_result = create_command_pool(*device_);
    if (!pool_result) {
        return std::unexpected(pool_result.error());
    }
    auto pool = raii{std::move(*pool_result)};

    auto command_result = pool->allocate();
    if (!command_result) {
        return std::unexpected(command_result.error());
    }
    auto command = raii{std::move(*command_result)};

    auto fence_result = create_fence(*device_, false);
    if (!fence_result) {
        return std::unexpected(fence_result.error());
    }
    auto fence_handle = raii{std::move(*fence_result)};

    if (auto reset = fence_handle->reset(); !reset) {
        return std::unexpected(reset.error());
    }
    if (auto begun = command->begin(); !begun) {
        return std::unexpected(begun.error());
    }

    const VkCommandBuffer command_native =
        reinterpret_cast<VkCommandBuffer>(command->native_handle());
    const VkImage image_native = reinterpret_cast<VkImage>(image_);

    record_layout_transition(command_native, image_native, VK_IMAGE_LAYOUT_UNDEFINED,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    record_clear(command_native, image_native, clear_color);
    record_layout_transition(command_native, image_native, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                             VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    if (auto ended = command->end(); !ended) {
        return std::unexpected(ended.error());
    }
    if (auto submitted = submit(*device_, command.value(), fence_handle.value()); !submitted) {
        return std::unexpected(submitted.error());
    }
    return fence_handle->wait(1000000000);
}

auto dma_buf_image::destroy() const noexcept -> void {
    vkDestroyImage(reinterpret_cast<VkDevice>(device_->native_handle()),
                   reinterpret_cast<VkImage>(image_), nullptr);
    vkFreeMemory(reinterpret_cast<VkDevice>(device_->native_handle()),
                 reinterpret_cast<VkDeviceMemory>(memory_), nullptr);
    ::close(fd_);
    image_ = nullptr;
    memory_ = nullptr;
    fd_ = -1;
}

}