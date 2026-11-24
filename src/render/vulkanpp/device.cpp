// SPDX-License-Identifier: Apache-2.0

#include <render/vulkanpp/device.hpp>

#include <limits>
#include <string_view>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

namespace {

auto enumerate_extensions(VkPhysicalDevice physical_device) -> std::vector<VkExtensionProperties> {
    u32 count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, nullptr) !=
            VK_SUCCESS ||
        count == 0) {
        return {};
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &count, extensions.data()) !=
        VK_SUCCESS) {
        return {};
    }
    return extensions;
}

auto has_extension(const std::vector<VkExtensionProperties>& extensions, const char* name) -> bool {
    for (const auto& extension : extensions) {
        if (std::string_view{extension.extensionName} == name) {
            return true;
        }
    }
    return false;
}

auto find_graphics_family(VkPhysicalDevice physical_device) -> u32 {
    u32 family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, nullptr);
    if (family_count == 0) {
        return std::numeric_limits<u32>::max();
    }

    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &family_count, families.data());
    for (u32 index = 0; index < family_count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            return index;
        }
    }
    return std::numeric_limits<u32>::max();
}

}

auto device::create(const instance& instance) noexcept -> result<device> {
    const VkInstance native_instance = reinterpret_cast<VkInstance>(instance.native_handle());

    u32 device_count = 0;
    VkResult result = vkEnumeratePhysicalDevices(native_instance, &device_count, nullptr);
    if (result != VK_SUCCESS || device_count == 0) {
        return std::unexpected(make_error(error_code::not_found, "no Vulkan physical device"));
    }

    std::vector<VkPhysicalDevice> physical_devices(device_count);
    result = vkEnumeratePhysicalDevices(native_instance, &device_count, physical_devices.data());
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkEnumeratePhysicalDevices failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    for (const VkPhysicalDevice physical_device : physical_devices) {
        const u32 queue_family = find_graphics_family(physical_device);
        if (queue_family == std::numeric_limits<u32>::max()) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        const auto extensions = enumerate_extensions(physical_device);
        const bool has_external_memory =
            has_extension(extensions, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        const bool has_external_memory_fd =
            has_extension(extensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        const bool has_dma_buf =
            has_extension(extensions, VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);

        std::vector<const char*> enabled_extensions;
        if (has_external_memory) {
            enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        }
        if (has_external_memory_fd) {
            enabled_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
        }
        if (has_dma_buf) {
            enabled_extensions.push_back(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
        }

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = 1;
        create_info.pQueueCreateInfos = &queue_info;
        create_info.enabledExtensionCount = static_cast<u32>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames =
            enabled_extensions.empty() ? nullptr : enabled_extensions.data();

        VkDevice handle = VK_NULL_HANDLE;
        result = vkCreateDevice(physical_device, &create_info, nullptr, &handle);
        if (result != VK_SUCCESS) {
            continue;
        }

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(handle, queue_family, 0, &queue);

        device_info info{
            .name = std::string{properties.deviceName},
            .vendor_id = properties.vendorID,
            .device_id = properties.deviceID,
            .api_version = properties.apiVersion,
            .driver_version = properties.driverVersion,
            .queue_family_index = queue_family,
        };
        return device{handle, queue, physical_device, std::move(info),
                      has_external_memory_fd && has_dma_buf};
    }

    return std::unexpected(make_error(error_code::not_found, "no usable Vulkan device"));
}

auto device::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto device::info() const noexcept -> const device_info& {
    return info_;
}

auto device::native_handle() const noexcept -> void* {
    return handle_;
}

auto device::native_queue() const noexcept -> void* {
    return queue_;
}

auto device::queue_family_index() const noexcept -> u32 {
    return info_.queue_family_index;
}

auto device::supports_dma_buf() const noexcept -> bool {
    return supports_dma_buf_;
}

auto device::find_memory_type(u32 type_bits, u32 required_flags) const noexcept -> result<u32> {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(reinterpret_cast<VkPhysicalDevice>(physical_), &properties);

    for (u32 index = 0; index < properties.memoryTypeCount; ++index) {
        if ((type_bits & (1U << index)) == 0) {
            continue;
        }
        if ((properties.memoryTypes[index].propertyFlags & required_flags) == required_flags) {
            return index;
        }
    }
    return std::unexpected(make_error(error_code::not_found, "no suitable memory type"));
}

auto device::destroy() const noexcept -> void {
    vkDestroyDevice(reinterpret_cast<VkDevice>(handle_), nullptr);
    handle_ = nullptr;
    queue_ = nullptr;
    physical_ = nullptr;
}

}