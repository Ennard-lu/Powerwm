// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/triangle.hpp>

#include <unistd.h>

#include <utility>
#include <vector>
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

struct external_format {
    VkFormat format = VK_FORMAT_UNDEFINED;
    dma_buf_pixel_format pixel_format = dma_buf_pixel_format::bgra8888;
    VkImageTiling tiling = VK_IMAGE_TILING_LINEAR;
    u64 modifier = 0;
    bool uses_modifier = false;
};

auto supports_external_format(VkPhysicalDevice physical, VkFormat format, VkImageTiling tiling,
                              bool uses_modifier, u64 modifier) -> bool {
    VkPhysicalDeviceExternalImageFormatInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier_info{};
    if (uses_modifier) {
        modifier_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
        modifier_info.drmFormatModifier = modifier;
        modifier_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        external_info.pNext = &modifier_info;
    }

    VkPhysicalDeviceImageFormatInfo2 format_info{};
    format_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    format_info.pNext = &external_info;
    format_info.format = format;
    format_info.type = VK_IMAGE_TYPE_2D;
    format_info.tiling = tiling;
    format_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    format_info.flags = 0;

    VkExternalImageFormatProperties external_properties{};
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    VkImageFormatProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    properties.pNext = &external_properties;
    if (vkGetPhysicalDeviceImageFormatProperties2(physical, &format_info, &properties) !=
        VK_SUCCESS) {
        return false;
    }

    const VkExternalMemoryFeatureFlags features =
        external_properties.externalMemoryProperties.externalMemoryFeatures;
    return (features & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) != 0;
}

auto find_modifier(VkPhysicalDevice physical, VkFormat format) -> u64 {
    VkDrmFormatModifierPropertiesListEXT list{};
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    VkFormatProperties2 format_properties{};
    format_properties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    format_properties.pNext = &list;
    vkGetPhysicalDeviceFormatProperties2(physical, format, &format_properties);
    if (list.drmFormatModifierCount == 0) {
        return 0;
    }

    std::vector<VkDrmFormatModifierPropertiesEXT> modifiers(list.drmFormatModifierCount);
    list.pDrmFormatModifierProperties = modifiers.data();
    list.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    format_properties.pNext = &list;
    vkGetPhysicalDeviceFormatProperties2(physical, format, &format_properties);

    for (const auto& modifier : modifiers) {
        if (modifier.drmFormatModifierPlaneCount == 1 && modifier.drmFormatModifier == 0 &&
            (modifier.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) !=
                0) {
            return 0;
        }
    }
    for (const auto& modifier : modifiers) {
        if (modifier.drmFormatModifierPlaneCount == 1 &&
            (modifier.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) !=
                0) {
            return modifier.drmFormatModifier;
        }
    }
    return 0;
}

auto select_external_format(const device& device, VkFormat format,
                            dma_buf_pixel_format pixel_format) -> external_format {
    const auto physical = reinterpret_cast<VkPhysicalDevice>(device.native_physical());
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    const auto get_modifier_properties =
        reinterpret_cast<PFN_vkGetImageDrmFormatModifierPropertiesEXT>(
            vkGetDeviceProcAddr(native_device, "vkGetImageDrmFormatModifierPropertiesEXT"));

    if (supports_external_format(physical, format, VK_IMAGE_TILING_LINEAR, false, 0)) {
        return external_format{
            .format = format,
            .pixel_format = pixel_format,
            .tiling = VK_IMAGE_TILING_LINEAR,
            .modifier = 0,
            .uses_modifier = false,
        };
    }

    if (get_modifier_properties != nullptr) {
        const u64 modifier = find_modifier(physical, format);
        if (supports_external_format(physical, format, VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                     true, modifier)) {
            return external_format{
                .format = format,
                .pixel_format = pixel_format,
                .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                .modifier = modifier,
                .uses_modifier = true,
            };
        }
    }
    return {};
}

auto select_format(const device& device) -> external_format {
    constexpr VkFormat candidates[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
    constexpr dma_buf_pixel_format pixel_formats[] = {dma_buf_pixel_format::bgra8888,
                                                      dma_buf_pixel_format::rgba8888};
    for (usize index = 0; index < std::size(candidates); ++index) {
        external_format selected =
            select_external_format(device, candidates[index], pixel_formats[index]);
        if (selected.format != VK_FORMAT_UNDEFINED) {
            return selected;
        }
    }
    return {};
}

auto create_external_image(const device& device, const external_format& format, u32 width_px,
                           u32 height_px, u64& actual_modifier) -> result<VkImage> {
    VkImageDrmFormatModifierListCreateInfoEXT modifier_info{};
    modifier_info.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
    modifier_info.drmFormatModifierCount = format.uses_modifier ? 1 : 0;
    modifier_info.pDrmFormatModifiers = format.uses_modifier ? &format.modifier : nullptr;

    VkExternalMemoryImageCreateInfo external_info{};
    external_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.pNext = format.uses_modifier ? &modifier_info : nullptr;
    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.pNext = &external_info;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format.format;
    image_info.extent = {width_px, height_px, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = format.tiling;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    VkImage image = VK_NULL_HANDLE;
    const VkResult result = vkCreateImage(native_device, &image_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImage failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    actual_modifier = 0;
    if (format.uses_modifier) {
        const auto get_properties = reinterpret_cast<PFN_vkGetImageDrmFormatModifierPropertiesEXT>(
            vkGetDeviceProcAddr(native_device, "vkGetImageDrmFormatModifierPropertiesEXT"));
        VkImageDrmFormatModifierPropertiesEXT properties{};
        properties.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
        if (get_properties == nullptr ||
            get_properties(native_device, image, &properties) != VK_SUCCESS) {
            vkDestroyImage(native_device, image, nullptr);
            return std::unexpected(
                make_error(error_code::unsupported, "DRM format modifier query failed"));
        }
        actual_modifier = properties.drmFormatModifier;
    }
    return image;
}

auto allocate_exported_memory(const device& device, VkImage image) -> result<VkDeviceMemory> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(native_device, image, &requirements);
    auto memory_type =
        device.find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory_type) {
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
    const VkResult result = vkAllocateMemory(native_device, &allocate_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return memory;
}

auto export_memory_fd(const device& device, VkDeviceMemory memory) -> result<handle_t> {
    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    const auto get_memory_fd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
        vkGetDeviceProcAddr(native_device, "vkGetMemoryFdKHR"));
    if (get_memory_fd == nullptr) {
        return std::unexpected(
            make_error(error_code::unsupported, "vkGetMemoryFdKHR is unavailable"));
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    handle_t fd = -1;
    const VkResult result = get_memory_fd(native_device, &fd_info, &fd);
    if (result != VK_SUCCESS || fd < 0) {
        return std::unexpected(make_error(error_code::io, "vkGetMemoryFdKHR failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }
    return fd;
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

    const external_format selected = select_format(device);
    if (selected.format == VK_FORMAT_UNDEFINED) {
        return std::unexpected(
            make_error(error_code::unsupported, "no exportable DMA-BUF image format"));
    }

    const VkDevice native_device = reinterpret_cast<VkDevice>(device.native_handle());
    u64 modifier = selected.modifier;
    auto image_result = create_external_image(device, selected, width_px, height_px, modifier);
    if (!image_result) {
        return std::unexpected(image_result.error());
    }
    const VkImage image = *image_result;

    auto memory_result = allocate_exported_memory(device, image);
    if (!memory_result) {
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(memory_result.error());
    }
    const VkDeviceMemory memory = *memory_result;

    const VkResult bind_result = vkBindImageMemory(native_device, image, memory, 0);
    if (bind_result != VK_SUCCESS) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindImageMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(bind_result)));
    }

    auto fd_result = export_memory_fd(device, memory);
    if (!fd_result) {
        vkFreeMemory(native_device, memory, nullptr);
        vkDestroyImage(native_device, image, nullptr);
        return std::unexpected(fd_result.error());
    }

    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(native_device, image, &subresource, &layout);

    return dma_buf_image{&device,
                         image,
                         memory,
                         *fd_result,
                         width_px,
                         height_px,
                         static_cast<u32>(layout.rowPitch),
                         selected.pixel_format,
                         modifier};
}

auto dma_buf_image::valid() const noexcept -> bool {
    return image_ != nullptr;
}

auto dma_buf_image::native_handle() const noexcept -> void* {
    return image_;
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

auto dma_buf_image::pixel_format() const noexcept -> dma_buf_pixel_format {
    return pixel_format_;
}

auto dma_buf_image::modifier() const noexcept -> u64 {
    return modifier_;
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

auto dma_buf_image::render_triangle(std::span<const std::byte> vertex_spirv,
                                    std::span<const std::byte> fragment_spirv) noexcept
    -> result<void> {
    return render_triangle_to_image(*device_, image_, width_px_, height_px_, vertex_spirv,
                                    fragment_spirv);
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