// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/texture.hpp>

#include <unistd.h>

#include <cstring>
#include <utility>
#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

struct staging_buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};

auto to_vk_format(texture_format format) -> VkFormat {
    return format == texture_format::bgra8 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
}

auto create_image_handle(VkDevice device, u32 width_px, u32 height_px, VkFormat format)
    -> result<VkImage> {
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = format;
    create_info.extent = {width_px, height_px, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    const VkResult result = vkCreateImage(device, &create_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImage failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return image;
}

auto allocate_image_memory(const device& device, VkImage image) -> result<void*> {
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(reinterpret_cast<VkDevice>(device.native_handle()), image,
                                 &requirements);

    auto memory_type =
        device.find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
        return std::unexpected(memory_type.error());
    }

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult result = vkAllocateMemory(reinterpret_cast<VkDevice>(device.native_handle()),
                                             &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    if (vkBindImageMemory(reinterpret_cast<VkDevice>(device.native_handle()), image, memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindImageMemory failed"));
    }
    return reinterpret_cast<void*>(memory);
}

auto create_view(VkDevice device, VkImage image, VkFormat format) -> result<VkImageView> {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format;
    create_info.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkImageView view = VK_NULL_HANDLE;
    const VkResult result = vkCreateImageView(device, &create_info, nullptr, &view);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImageView failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return view;
}

auto create_sampler(VkDevice device) -> result<VkSampler> {
    VkSamplerCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    create_info.magFilter = VK_FILTER_LINEAR;
    create_info.minFilter = VK_FILTER_LINEAR;
    create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    create_info.maxAnisotropy = 1.0F;

    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult result = vkCreateSampler(device, &create_info, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateSampler failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return sampler;
}

auto create_staging(const device& device, VkDeviceSize size_bytes) -> result<staging_buffer> {
    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size_bytes;
    create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(reinterpret_cast<VkDevice>(device.native_handle()),
                                     &create_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateBuffer failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(reinterpret_cast<VkDevice>(device.native_handle()), buffer,
                                  &requirements);
    auto memory_type = device.find_memory_type(requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memory_type) {
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), buffer, nullptr);
        return std::unexpected(memory_type.error());
    }

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = *memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(reinterpret_cast<VkDevice>(device.native_handle()), &allocate_info,
                              nullptr, &memory);
    if (result != VK_SUCCESS) {
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), buffer, nullptr);
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    if (vkBindBufferMemory(reinterpret_cast<VkDevice>(device.native_handle()), buffer, memory, 0) !=
        VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), buffer, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindBufferMemory failed"));
    }

    void* mapped = nullptr;
    if (vkMapMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, 0, VK_WHOLE_SIZE, 0,
                    &mapped) != VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), buffer, nullptr);
        return std::unexpected(make_error(error_code::io, "vkMapMemory failed"));
    }
    return staging_buffer{buffer, memory, mapped};
}

auto destroy_staging(const device& device, staging_buffer& staging) -> void {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    vkUnmapMemory(native_device, staging.memory);
    vkDestroyBuffer(native_device, staging.buffer, nullptr);
    vkFreeMemory(native_device, staging.memory, nullptr);
    staging = {};
}

auto record_transition(VkCommandBuffer command, VkImage image, VkImageLayout old_layout,
                       VkImageLayout new_layout) -> void {
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
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;

    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
}

auto record_upload(VkCommandBuffer command, VkImage image, VkBuffer staging, bool initialized,
                   u32 width_px, u32 height_px, u32 stride_bytes) -> void {
    record_transition(command, image,
                      initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                  : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy_region{};
    copy_region.imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    copy_region.imageExtent = {width_px, height_px, 1};
    copy_region.bufferRowLength = stride_bytes / 4;
    vkCmdCopyBufferToImage(command, staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copy_region);
    record_transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

auto upload_pixels(const device& device, void* image_handle, bool initialized, u32 width_px,
                   u32 height_px, const texture_data& data) -> result<void> {
    if (data.stride_bytes < width_px * 4 || data.stride_bytes % 4 != 0 ||
        data.pixels.size() < static_cast<usize>(data.stride_bytes) * height_px) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "texture stride or size is invalid"));
    }
    const VkDeviceSize size_bytes =
        static_cast<VkDeviceSize>(data.stride_bytes) * static_cast<VkDeviceSize>(height_px);

    auto staging_result = create_staging(device, size_bytes);
    if (!staging_result) {
        return std::unexpected(staging_result.error());
    }
    staging_buffer staging = *staging_result;
    std::memcpy(staging.mapped, data.pixels.data(), static_cast<usize>(size_bytes));

    auto pool_result = create_command_pool(device);
    if (!pool_result) {
        destroy_staging(device, staging);
        return std::unexpected(pool_result.error());
    }
    auto pool = raii{std::move(*pool_result)};

    auto command_result = pool->allocate();
    if (!command_result) {
        destroy_staging(device, staging);
        return std::unexpected(command_result.error());
    }
    auto command = raii{std::move(*command_result)};

    auto fence_result = create_fence(device, false);
    if (!fence_result) {
        destroy_staging(device, staging);
        return std::unexpected(fence_result.error());
    }
    auto fence = raii{std::move(*fence_result)};

    if (auto reset = fence->reset(); !reset) {
        destroy_staging(device, staging);
        return std::unexpected(reset.error());
    }
    if (auto begun = command->begin(); !begun) {
        destroy_staging(device, staging);
        return std::unexpected(begun.error());
    }

    record_upload(reinterpret_cast<VkCommandBuffer>(command->native_handle()),
                  reinterpret_cast<VkImage>(image_handle), staging.buffer, initialized, width_px,
                  height_px, data.stride_bytes);

    if (auto ended = command->end(); !ended) {
        destroy_staging(device, staging);
        return std::unexpected(ended.error());
    }
    if (auto submitted = submit(device, command.value(), fence.value()); !submitted) {
        destroy_staging(device, staging);
        return std::unexpected(submitted.error());
    }
    if (auto waited = fence->wait(1000000000); !waited) {
        destroy_staging(device, staging);
        return std::unexpected(waited.error());
    }

    destroy_staging(device, staging);
    return {};
}

auto fourcc_to_vk_format(u32 fourcc) -> VkFormat {
    constexpr u32 argb8888 = 0x34325241;
    constexpr u32 xrgb8888 = 0x34325258;
    constexpr u32 abgr8888 = 0x34324241;
    constexpr u32 xbgr8888 = 0x34324258;
    switch (fourcc) {
    case argb8888:
    case xrgb8888:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case abgr8888:
    case xbgr8888:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        return VK_FORMAT_UNDEFINED;
    }
}

auto create_imported_image(const device& device, u32 width_px, u32 height_px, VkFormat format,
                           u64 modifier) -> result<VkImage> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    const bool uses_modifier =
        vkGetDeviceProcAddr(native_device, "vkGetImageDrmFormatModifierPropertiesEXT") != nullptr;

    VkImageDrmFormatModifierListCreateInfoEXT modifier_info{};
    modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
    modifier_info.drmFormatModifierCount = 1;
    modifier_info.pDrmFormatModifiers = &modifier;

    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.pNext = uses_modifier ? &modifier_info : nullptr;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.pNext = &external_info;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = format;
    create_info.extent = {width_px, height_px, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling =
        uses_modifier ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : VK_IMAGE_TILING_LINEAR;
    create_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!uses_modifier && modifier != 0) {
        return std::unexpected(
            make_error(error_code::unsupported, "DRM format modifiers are unavailable"));
    }

    VkImage image = VK_NULL_HANDLE;
    const VkResult result = vkCreateImage(native_device, &create_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImage failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return image;
}

auto import_image_memory(const device& device, VkImage image, handle_t fd)
    -> result<VkDeviceMemory> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    const auto get_fd_properties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
        vkGetDeviceProcAddr(native_device, "vkGetMemoryFdPropertiesKHR"));
    if (get_fd_properties == nullptr) {
        return std::unexpected(
            make_error(error_code::unsupported, "vkGetMemoryFdPropertiesKHR is unavailable"));
    }
    const handle_t owned = ::dup(fd);
    if (owned < 0) {
        return std::unexpected(
            make_error(error_code::io, "dup failed", std::source_location::current(), errno));
    }

    VkMemoryFdPropertiesKHR fd_properties{};
    fd_properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    VkResult result = get_fd_properties(
        native_device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, owned, &fd_properties);
    if (result != VK_SUCCESS) {
        ::close(owned);
        return std::unexpected(make_error(error_code::io, "vkGetMemoryFdPropertiesKHR failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(native_device, image, &requirements);
    const u32 candidates = requirements.memoryTypeBits & fd_properties.memoryTypeBits;
    if (candidates == 0) {
        ::close(owned);
        return std::unexpected(
            make_error(error_code::unsupported, "no memory type accepts the DMA-BUF"));
    }

    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(
        reinterpret_cast<VkPhysicalDevice>(device.native_physical()), &memory_properties);
    u32 memory_type = UINT32_MAX;
    for (u32 index = 0; index < memory_properties.memoryTypeCount; ++index) {
        if ((candidates & (1U << index)) == 0) {
            continue;
        }
        if (memory_type == UINT32_MAX) {
            memory_type = index;
        }
        if ((memory_properties.memoryTypes[index].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
            memory_type = index;
            break;
        }
    }
    if (memory_type == UINT32_MAX) {
        ::close(owned);
        return std::unexpected(
            make_error(error_code::unsupported, "DMA-BUF memory type is invalid"));
    }

    VkMemoryDedicatedAllocateInfo dedicated_info{};
    dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.image = image;

    VkImportMemoryFdInfoKHR import_info{};
    import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.pNext = &dedicated_info;
    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    import_info.fd = owned;

    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext = &import_info;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    result = vkAllocateMemory(native_device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        ::close(owned);
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return memory;
}

auto transition_to_sampled(const device& device, VkImage image) -> result<void> {
    auto pool_result = create_command_pool(device);
    if (!pool_result) {
        return std::unexpected(pool_result.error());
    }
    auto pool = raii{std::move(*pool_result)};
    auto command_result = pool->allocate();
    if (!command_result) {
        return std::unexpected(command_result.error());
    }
    auto command = raii{std::move(*command_result)};
    auto fence_result = create_fence(device, false);
    if (!fence_result) {
        return std::unexpected(fence_result.error());
    }
    auto fence = raii{std::move(*fence_result)};
    if (auto reset = fence->reset(); !reset) {
        return std::unexpected(reset.error());
    }
    if (auto begun = command->begin(); !begun) {
        return std::unexpected(begun.error());
    }

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(reinterpret_cast<VkCommandBuffer>(command->native_handle()),
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (auto ended = command->end(); !ended) {
        return std::unexpected(ended.error());
    }
    if (auto submitted = submit(device, command.value(), fence.value()); !submitted) {
        return std::unexpected(submitted.error());
    }
    return fence->wait(1000000000);
}

}

auto create_dma_buf_texture(const device& device, const dma_buf_texture_data& data) noexcept
    -> result<texture> {
    if (!device.supports_dma_buf()) {
        return std::unexpected(
            make_error(error_code::unsupported, "Vulkan DMA-BUF import is unavailable"));
    }
    if (data.fd < 0 || data.width_px == 0 || data.height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "DMA-BUF texture arguments are invalid"));
    }
    const VkFormat format = fourcc_to_vk_format(data.fourcc);
    if (format == VK_FORMAT_UNDEFINED) {
        return std::unexpected(
            make_error(error_code::unsupported, "DMA-BUF fourcc is unsupported"));
    }

    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    auto image_result =
        create_imported_image(device, data.width_px, data.height_px, format, data.modifier);
    if (!image_result) {
        return std::unexpected(image_result.error());
    }
    const VkImage image = *image_result;

    auto memory_result = import_image_memory(device, image, data.fd);
    if (!memory_result) {
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(memory_result.error());
    }
    const VkDeviceMemory memory = *memory_result;

    if (vkBindImageMemory(native_device, image, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindImageMemory failed"));
    }

    auto view_result = create_view(native_device, image, format);
    if (!view_result) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(view_result.error());
    }
    auto sampler_result = create_sampler(native_device);
    if (!sampler_result) {
        vkDestroyImageView(native_device, *view_result, nullptr);
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(sampler_result.error());
    }

    if (auto transitioned = transition_to_sampled(device, image); !transitioned) {
        vkDestroySampler(native_device, *sampler_result, nullptr);
        vkDestroyImageView(native_device, *view_result, nullptr);
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(transitioned.error());
    }

    const texture_format texture_format_value =
        format == VK_FORMAT_B8G8R8A8_UNORM ? texture_format::bgra8 : texture_format::rgba8;
    return texture{&device, image,         memory,         *view_result,        *sampler_result,
                   true,    data.width_px, data.height_px, texture_format_value};
}

auto create_texture(const device& device, u32 width_px, u32 height_px,
                    const texture_data& data) noexcept -> result<texture> {
    if (width_px == 0 || height_px == 0) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "texture size must be non-zero"));
    }
    if (data.stride_bytes < width_px * 4 || data.stride_bytes % 4 != 0 ||
        data.pixels.size() < static_cast<usize>(data.stride_bytes) * height_px) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "texture pixel size does not match"));
    }

    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    auto image_result =
        create_image_handle(native_device, width_px, height_px, to_vk_format(data.format));
    if (!image_result) {
        return std::unexpected(image_result.error());
    }
    VkImage image = *image_result;

    auto memory_result = allocate_image_memory(device, image);
    if (!memory_result) {
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(memory_result.error());
    }

    auto view_result = create_view(native_device, image, to_vk_format(data.format));
    if (!view_result) {
        vkFreeMemory(native_device, reinterpret_cast<VkDeviceMemory>(*memory_result), nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(view_result.error());
    }

    auto sampler_result = create_sampler(native_device);
    if (!sampler_result) {
        vkDestroyImageView(native_device, *view_result, nullptr);
        vkFreeMemory(native_device, reinterpret_cast<VkDeviceMemory>(*memory_result), nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(sampler_result.error());
    }

    texture result{&device, image,    *memory_result, *view_result, *sampler_result,
                   false,   width_px, height_px,      data.format};
    if (auto uploaded = result.update(data); !uploaded) {
        result.destroy();
        return std::unexpected(uploaded.error());
    }
    result.initialized_ = true;
    return result;
}

auto texture::valid() const noexcept -> bool {
    return image_ != nullptr;
}

auto texture::width_px() const noexcept -> u32 {
    return width_px_;
}

auto texture::height_px() const noexcept -> u32 {
    return height_px_;
}

auto texture::native_view() const noexcept -> void* {
    return view_;
}

auto texture::native_sampler() const noexcept -> void* {
    return sampler_;
}

auto create_texture(const device& device, u32 width_px, u32 height_px,
                    std::span<const std::byte> rgba8) noexcept -> result<texture> {
    return create_texture(device, width_px, height_px,
                          texture_data{.pixels = rgba8,
                                       .stride_bytes = width_px * 4,
                                       .format = texture_format::rgba8});
}

auto texture::update(std::span<const std::byte> rgba8) noexcept -> result<void> {
    return update(texture_data{
        .pixels = rgba8, .stride_bytes = width_px_ * 4, .format = texture_format::rgba8});
}

auto texture::update(const texture_data& data) noexcept -> result<void> {
    if (data.format != format_) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "texture format cannot change"));
    }
    return upload_pixels(*device_, image_, initialized_, width_px_, height_px_, data);
}

auto texture::destroy() const noexcept -> void {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device_->native_handle());
    vkDestroySampler(native_device, reinterpret_cast<VkSampler>(sampler_), nullptr);
    vkDestroyImageView(native_device, reinterpret_cast<VkImageView>(view_), nullptr);
    vkFreeMemory(native_device, reinterpret_cast<VkDeviceMemory>(memory_), nullptr);
    vkDestroyImage(native_device, reinterpret_cast<VkImage>(image_), nullptr);
    image_ = nullptr;
    memory_ = nullptr;
    view_ = nullptr;
    sampler_ = nullptr;
}

}
