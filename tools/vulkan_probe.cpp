// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/instance.hpp>

#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace vk = pwm::vulkanpp;

auto require_instance(pwm::result<vk::instance>&& value) -> vk::instance {
    if (!value) {
        std::println(stderr, "error: {} (system={})", value.error().message,
                     value.error().system_error);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto require_device(pwm::result<vk::device>&& value) -> vk::device {
    if (!value) {
        std::println(stderr, "error: {} (system={})", value.error().message,
                     value.error().system_error);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto run_probe() -> int {
    const vk::application_info application{
        .name = "powerwm_vulkan_probe",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };

    auto instance = pwm::raii{require_instance(vk::instance::create(application))};
    auto device = pwm::raii{require_device(vk::device::create(instance.value()))};

    const vk::device_info& info = device->info();
    std::println("vulkan device: {}", info.name);
    std::println("vendor=0x{:04x} device=0x{:04x}", info.vendor_id, info.device_id);
    std::println("api={} driver={} queue-family={}", info.api_version, info.driver_version,
                 info.queue_family_index);
    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}