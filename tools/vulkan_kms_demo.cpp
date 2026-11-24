// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/instance.hpp>

#include <cstdlib>
#include <print>
#include <thread>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;
namespace vk = pwm::vulkanpp;

using namespace std::chrono_literals;

struct output_properties {
    drm::prop::id::crtc crtc;
    drm::prop::id::connector connector;
    drm::prop::id::plane plane;
};

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

auto enable_drm(drm::device& device) -> void {
    if (!device.is_master()) {
        require_value(device.set_master());
    }
    require_value(device.set_atomic_enabled(true));
    require_value(device.set_universal_planes_enabled(true));
}

auto lookup_output_properties(drm::device& device, const drm::output_target& output)
    -> output_properties {
    auto crtc_properties = pwm::raii{device.get_crtc_properties(output.crtc_id)};
    auto connector_properties = pwm::raii{device.get_connector_properties(output.connector_id)};
    auto plane_properties = pwm::raii{device.get_plane_properties(output.plane_id)};

    if (!crtc_properties->valid() || !connector_properties->valid() || !plane_properties->valid()) {
        pwm::terminate_with_message("failed to query DRM properties");
    }

    return {
        .crtc = drm::lookup_property_ids<drm::prop::id::crtc>(device, crtc_properties.value()),
        .connector = drm::lookup_property_ids<drm::prop::id::connector>(
            device, connector_properties.value()),
        .plane = drm::lookup_property_ids<drm::prop::id::plane>(device, plane_properties.value()),
    };
}

auto add_required_property(drm::atomic_request& request, pwm::u32 object_id, pwm::u32 property_id,
                           pwm::u64 value) -> void {
    if (request.add_property(object_id, property_id, value) < 0) {
        pwm::terminate_with_message("drmModeAtomicAddProperty failed");
    }
}

auto add_output_properties(const drm::output_target& output, const output_properties& ids,
                           const drm::property_blob& blob,
                           const drm::dma_buf_framebuffer& framebuffer,
                           drm::atomic_request& request) -> void {
    add_required_property(request, output.crtc_id, ids.crtc.active, 1);
    add_required_property(request, output.crtc_id, ids.crtc.mode_id, blob.id());
    add_required_property(request, output.connector_id, ids.connector.crtc_id, output.crtc_id);

    add_required_property(request, output.plane_id, ids.plane.crtc_id, output.crtc_id);
    add_required_property(request, output.plane_id, ids.plane.fb_id, framebuffer.id());
    add_required_property(request, output.plane_id, ids.plane.crtc_x, 0);
    add_required_property(request, output.plane_id, ids.plane.crtc_y, 0);
    add_required_property(request, output.plane_id, ids.plane.crtc_w, output.mode.width_px());
    add_required_property(request, output.plane_id, ids.plane.crtc_h, output.mode.height_px());
    add_required_property(request, output.plane_id, ids.plane.src_x, 0);
    add_required_property(request, output.plane_id, ids.plane.src_y, 0);
    add_required_property(request, output.plane_id, ids.plane.src_w, output.mode.width_px() << 16);
    add_required_property(request, output.plane_id, ids.plane.src_h, output.mode.height_px() << 16);
}

auto present(drm::device& device, const drm::output_target& output, const output_properties& ids,
             const drm::property_blob& blob, const drm::dma_buf_framebuffer& framebuffer,
             bool allow_modeset) -> void {
    auto request = pwm::raii{require_value(drm::atomic_request::create())};
    add_output_properties(output, ids, blob, framebuffer, request.value());

    pwm::u32 flags = drm::atomic_page_flip_event | drm::atomic_nonblock;
    if (allow_modeset) {
        flags |= drm::atomic_allow_modeset;
    }
    require_value(device.commit_atomic(request.value(), flags));
    require_value(device.wait_for_page_flip(1000));
}

auto run_demo() -> int {
    auto drm_device = pwm::raii{require_value(drm::device::open_first_available())};
    enable_drm(drm_device.value());

    const drm::output_target output = require_value(drm_device->find_primary_output());
    const output_properties ids = lookup_output_properties(drm_device.value(), output);
    auto blob = pwm::raii{require_value(drm_device->create_property_blob(output.mode.bytes()))};

    const vk::application_info application{
        .name = "powerwm_vulkan_kms_demo",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };
    auto instance = pwm::raii{require_value(vk::instance::create(application))};
    auto device = pwm::raii{require_value(vk::device::create(instance.value()))};
    if (!device->supports_dma_buf()) {
        pwm::terminate_with_message("Vulkan DMA-BUF export is unavailable");
    }

    auto first_image = pwm::raii{require_value(
        vk::create_dma_buf_image(device.value(), output.mode.width_px(), output.mode.height_px()))};
    auto second_image = pwm::raii{require_value(
        vk::create_dma_buf_image(device.value(), output.mode.width_px(), output.mode.height_px()))};
    require_value(first_image->clear({.red = 0.20F, .green = 0.45F, .blue = 0.80F, .alpha = 1.0F}));
    require_value(
        second_image->clear({.red = 0.80F, .green = 0.30F, .blue = 0.20F, .alpha = 1.0F}));

    auto first_framebuffer = pwm::raii{require_value(drm_device->create_framebuffer_from_dma_buf(
        first_image->fd(), first_image->width_px(), first_image->height_px(),
        first_image->pitch_bytes(), drm::format_xrgb8888, 0))};
    auto second_framebuffer = pwm::raii{require_value(drm_device->create_framebuffer_from_dma_buf(
        second_image->fd(), second_image->width_px(), second_image->height_px(),
        second_image->pitch_bytes(), drm::format_xrgb8888, 0))};

    present(drm_device.value(), output, ids, blob.value(), first_framebuffer.value(), true);
    std::this_thread::sleep_for(1s);
    present(drm_device.value(), output, ids, blob.value(), second_framebuffer.value(), false);
    std::this_thread::sleep_for(1s);
    present(drm_device.value(), output, ids, blob.value(), first_framebuffer.value(), false);

    if (drm_device->is_master()) {
        require_value(drm_device->drop_master());
    }
    std::println("vulkan dma-buf kms presentation ok");
    return EXIT_SUCCESS;
}

}

int main() {
    return run_demo();
}