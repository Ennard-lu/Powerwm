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
}

auto run_probe() -> int {
    auto device = pwm::raii{require_value(drm::device::open_first_available())};
    print_outputs(device.value());
    return EXIT_SUCCESS;
}

}

int main() {
    return run_probe();
}