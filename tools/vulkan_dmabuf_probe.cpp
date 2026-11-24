// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/instance.hpp>

#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;
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
        .name = "powerwm_vulkan_dmabuf_probe",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };

    auto instance = pwm::raii{require_value(vk::instance::create(application))};
    auto device = pwm::raii{require_value(vk::device::create(instance.value()))};
    if (!device->supports_dma_buf()) {
        std::println(stderr, "error: Vulkan DMA-BUF export is unsupported");
        return EXIT_FAILURE;
    }

    auto image = pwm::raii{require_value(vk::create_dma_buf_image(device.value(), 64, 64))};
    require_value(image->clear({.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 1.0F}));

    auto drm_device = pwm::raii{require_value(drm::device::open_first_available())};
    auto framebuffer = pwm::raii{require_value(drm_device->create_framebuffer_from_dma_buf(
        image->fd(), image->width_px(), image->height_px(), image->pitch_bytes(),
        drm::format_xrgb8888, 0))};

    std::println("dma-buf fd={} pitch={} framebuffer={}", image->fd(), image->pitch_bytes(),
                 framebuffer->id());
    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}