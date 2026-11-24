// SPDX-License-Identifier: Apache-2.0

#include <render/vulkanpp/image.hpp>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

auto create_image_memory(const device& device, VkImage image, VkMemoryPropertyFlags properties)
    -> result<void*> {
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(reinterpret_cast<VkDevice>(device.native_handle()),
                                 reinterpret_cast<VkImage>(image), &requirements);

    auto memory_type = device.find_memory_type(requirements.memoryTypeBits, properties);
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

    const VkResult bind_result =
        vkBindImageMemory(reinterpret_cast<VkDevice>(device.native_handle()),
                          reinterpret_cast<VkImage>(image), memory, 0);
    if (bind_result != VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindImageMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(bind_result)));
    }
    return reinterpret_cast<void*>(memory);
}

}

auto create_image(const device& device, u32 width_px, u32 height_px) noexcept -> result<image> {
    VkImageCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    create_info.extent = {width_px, height_px, 1};
    create_info.mipLevels = 1;
    create_info.arrayLayers = 1;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage handle = VK_NULL_HANDLE;
    VkResult result = vkCreateImage(reinterpret_cast<VkDevice>(device.native_handle()),
                                    &create_info, nullptr, &handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateImage failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    auto memory = create_image_memory(device, handle, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memory) {
        vkDestroyImage(reinterpret_cast<VkDevice>(device.native_handle()), handle, nullptr);
        return std::unexpected(memory.error());
    }

    return image{device.native_handle(), handle, *memory};
}

auto create_readback_buffer(const device& device, u64 size_bytes) noexcept -> result<buffer> {
    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = size_bytes;
    create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer handle = VK_NULL_HANDLE;
    VkResult result = vkCreateBuffer(reinterpret_cast<VkDevice>(device.native_handle()),
                                     &create_info, nullptr, &handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateBuffer failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(reinterpret_cast<VkDevice>(device.native_handle()),
                                  reinterpret_cast<VkBuffer>(handle), &requirements);

    auto memory_type = device.find_memory_type(requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memory_type) {
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), handle, nullptr);
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
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), handle, nullptr);
        return std::unexpected(make_error(error_code::resource_exhausted, "vkAllocateMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    result = vkBindBufferMemory(reinterpret_cast<VkDevice>(device.native_handle()),
                                reinterpret_cast<VkBuffer>(handle), memory, 0);
    if (result != VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), handle, nullptr);
        return std::unexpected(make_error(error_code::io, "vkBindBufferMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    void* mapped = nullptr;
    result = vkMapMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, 0,
                         VK_WHOLE_SIZE, 0, &mapped);
    if (result != VK_SUCCESS) {
        vkFreeMemory(reinterpret_cast<VkDevice>(device.native_handle()), memory, nullptr);
        vkDestroyBuffer(reinterpret_cast<VkDevice>(device.native_handle()), handle, nullptr);
        return std::unexpected(make_error(error_code::io, "vkMapMemory failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    return buffer{device.native_handle(), handle, memory, mapped};
}

auto image::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto image::native_handle() const noexcept -> void* {
    return handle_;
}

auto image::destroy() const noexcept -> void {
    vkDestroyImage(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkImage>(handle_),
                   nullptr);
    vkFreeMemory(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkDeviceMemory>(memory_),
                 nullptr);
    handle_ = nullptr;
    memory_ = nullptr;
}

auto buffer::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto buffer::native_handle() const noexcept -> void* {
    return handle_;
}

auto buffer::mapped_data() const noexcept -> void* {
    return mapped_;
}

auto buffer::destroy() const noexcept -> void {
    vkUnmapMemory(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkDeviceMemory>(memory_));
    vkDestroyBuffer(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkBuffer>(handle_),
                    nullptr);
    vkFreeMemory(reinterpret_cast<VkDevice>(device_), reinterpret_cast<VkDeviceMemory>(memory_),
                 nullptr);
    handle_ = nullptr;
    memory_ = nullptr;
    mapped_ = nullptr;
}

}