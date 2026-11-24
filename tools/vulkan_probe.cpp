// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <render/vulkanpp/command_pool.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/fence.hpp>
#include <render/vulkanpp/instance.hpp>
#include <render/vulkanpp/offscreen.hpp>

#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace vk = pwm::vulkanpp;

auto require_value(pwm::result<void>&& value) -> void {
    if (!value) {
        std::println(stderr, "error: {} (system={})", value.error().message,
                     value.error().system_error);
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
auto require_value(pwm::result<T>&& value) -> T {
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

    auto instance = pwm::raii{require_value(vk::instance::create(application))};
    auto device = pwm::raii{require_value(vk::device::create(instance.value()))};

    const vk::device_info& info = device->info();
    std::println("vulkan device: {}", info.name);
    std::println("vendor=0x{:04x} device=0x{:04x}", info.vendor_id, info.device_id);
    std::println("api={} driver={} queue-family={}", info.api_version, info.driver_version,
                 info.queue_family_index);

    auto pool = pwm::raii{require_value(vk::create_command_pool(device.value()))};
    auto command_buffer = pwm::raii{require_value(pool->allocate())};
    auto fence = pwm::raii{require_value(vk::create_fence(device.value(), false))};
    require_value(command_buffer->begin());
    require_value(command_buffer->end());
    require_value(vk::submit(device.value(), command_buffer.value(), fence.value()));
    require_value(fence->wait(1000000000));
    std::println("vulkan queue submit ok");

    auto target = pwm::raii{require_value(vk::create_offscreen_target(device.value(), 8, 8))};
    require_value(target->render({.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 1.0F}));
    const vk::color pixel = require_value(target->read_pixel(0, 0));
    std::println("clear pixel r={:.2f} g={:.2f} b={:.2f} a={:.2f}", pixel.red, pixel.green,
                 pixel.blue, pixel.alpha);

    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}