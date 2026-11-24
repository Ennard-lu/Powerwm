// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>

#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;

auto require_value(pwm::result<drm::device>&& value) -> drm::device {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto print_property_ids(drm::device& device, drm::property_set properties) -> void {
    for (const pwm::u32 property_id : properties.property_ids()) {
        auto info = pwm::raii{device.get_property_info(property_id)};
        std::println("    property {}={}", property_id, info->name());
    }
}

auto print_outputs(drm::device& device) -> void {
    auto resources = pwm::raii{device.get_resources()};
    if (!resources->valid()) {
        std::println(stderr, "error: failed to query DRM resources");
        return;
    }

    std::println("crtcs={} connectors={} framebuffers={} encoders={}", resources->crtc_ids().size(),
                 resources->connector_ids().size(), resources->framebuffer_ids().size(),
                 resources->encoder_ids().size());

    for (const pwm::u32 connector_id : resources->connector_ids()) {
        auto connector = pwm::raii{device.get_connector(connector_id)};
        if (!connector->valid()) {
            continue;
        }
        std::println("connector id={} connected={} modes={}", connector->id(),
                     connector->is_connected(), connector->mode_count());
        if (connector->mode_count() > 0) {
            const drm::display_mode mode = connector->mode(0);
            std::println("  preferred {}x{}@{}Hz", mode.width_px(), mode.height_px(),
                         mode.refresh_hz());
        }
    }

    const pwm::u32 crtc_id = resources->crtc_ids().front();
    auto crtc_properties = pwm::raii{device.get_crtc_properties(crtc_id)};
    std::println("crtc {} properties:", crtc_id);
    print_property_ids(device, crtc_properties.value());

    const pwm::u32 connector_id = resources->connector_ids().front();
    auto connector_properties = pwm::raii{device.get_connector_properties(connector_id)};
    std::println("connector {} properties:", connector_id);
    print_property_ids(device, connector_properties.value());

    auto planes = pwm::raii{device.get_plane_resources()};
    const pwm::u32 plane_id = planes->plane_ids().front();
    auto plane_properties = pwm::raii{device.get_plane_properties(plane_id)};
    std::println("plane {} properties:", plane_id);
    print_property_ids(device, plane_properties.value());
}

auto run_probe() -> int {
    auto device = pwm::raii{require_value(drm::device::open_first_available())};
    if (auto enabled = device->set_atomic_enabled(true); !enabled) {
        std::println(stderr, "warning: atomic cap unavailable");
    }
    if (auto enabled = device->set_universal_planes_enabled(true); !enabled) {
        std::println(stderr, "warning: universal planes unavailable");
    }
    print_outputs(device.value());
    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}