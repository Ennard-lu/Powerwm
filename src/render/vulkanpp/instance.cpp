// SPDX-License-Identifier: Apache-2.0

#include <render/vulkanpp/instance.hpp>

#include <vulkan/vulkan.h>

namespace pwm::vulkanpp {

auto instance::create(const application_info& info) noexcept -> result<instance> {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = info.name.c_str();
    application.applicationVersion = info.version;
    application.pEngineName = info.engine_name.c_str();
    application.engineVersion = info.engine_version;
    application.apiVersion = info.api_version == 0 ? VK_API_VERSION_1_3 : info.api_version;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &application;

    VkInstance handle = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&create_info, nullptr, &handle);
    if (result != VK_SUCCESS) {
        return std::unexpected(make_error(error_code::io, "vkCreateInstance failed",
                                          std::source_location::current(),
                                          static_cast<i32>(result)));
    }

    return instance{reinterpret_cast<void*>(handle)};
}

auto instance::valid() const noexcept -> bool {
    return handle_ != nullptr;
}

auto instance::native_handle() const noexcept -> void* {
    return handle_;
}

auto instance::destroy() const noexcept -> void {
    vkDestroyInstance(reinterpret_cast<VkInstance>(handle_), nullptr);
    handle_ = nullptr;
}

}