// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <print>
#include <span>
#include <thread>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;

using namespace std::chrono_literals;

struct output_selection {
    pwm::u32 crtc_id;
    pwm::u32 connector_id;
    pwm::u32 width_px;
    pwm::u32 height_px;
    drm::display_mode mode;
};

auto require_value(pwm::result<void>&& value) -> void {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
}

template <typename T>
auto require_value(pwm::result<T>&& value) -> T {
    if (!value) {
        std::println(stderr, "error: {}", value.error().message);
        std::exit(EXIT_FAILURE);
    }
    return std::move(*value);
}

auto open_drm_device() -> pwm::raii<drm::device> {
    auto device = require_value(drm::device::create("/dev/dri/card1"));
    return pwm::raii{std::move(device)};
}

auto enable_atomic_features(drm::device& device) -> void {
    require_value(device.set_master());
    require_value(device.set_atomic_enabled(true));
    require_value(device.set_universal_planes_enabled(true));
}

auto select_output(drm::device& device) -> output_selection {
    auto resources = pwm::raii{device.get_resources()};
    if (!resources->valid() || resources->crtc_ids().empty() ||
        resources->connector_ids().empty()) {
        pwm::terminate_with_message("DRM device has no usable CRTC or connector");
    }

    for (const pwm::u32 connector_id : resources->connector_ids()) {
        auto connector = pwm::raii{device.get_connector(connector_id)};
        if (!connector->valid() || connector->mode_count() == 0) {
            continue;
        }
        const drm::display_mode mode = connector->mode(0);
        return {
            .crtc_id = resources->crtc_ids().front(),
            .connector_id = connector_id,
            .width_px = mode.width_px(),
            .height_px = mode.height_px(),
            .mode = mode,
        };
    }

    pwm::terminate_with_message("DRM device has no connected output");
}

auto make_buffer_format(const output_selection& output) -> drm::buffer_format {
    return {
        .width_px = output.width_px,
        .height_px = output.height_px,
        .pitch_bytes = 0,
        .depth_bits = 24,
        .bpp = 32,
        .size_bytes = 0,
    };
}

auto create_buffer(drm::device& device, const drm::buffer_format& format)
    -> pwm::raii<drm::dumb_buffer> {
    auto buffer = require_value(device.create_dumb_buffer(format));
    return pwm::raii{std::move(buffer)};
}

auto map_buffer(const drm::dumb_buffer& buffer) -> pwm::raii<drm::mapped_memory> {
    auto mapped = require_value(buffer.map());
    return pwm::raii{std::move(mapped)};
}

auto fill_test_pattern(drm::mapped_memory& memory, const drm::buffer_format& format) -> void {
    auto* pixels = static_cast<pwm::u32*>(memory.address);
    const pwm::usize row_stride = format.pitch_bytes / sizeof(pwm::u32);
    const pwm::i64 center_x = static_cast<pwm::i64>(format.width_px) / 2;

    for (pwm::u32 y = 0; y < format.height_px; ++y) {
        auto* row = pixels + static_cast<pwm::usize>(y) * row_stride;
        for (pwm::u32 x = 0; x < format.width_px; ++x) {
            const pwm::i64 distance = std::abs(static_cast<pwm::i64>(x) - center_x);
            row[x] = distance < static_cast<pwm::i64>(y) ? 0xFFFFFF : 0x000000;
        }
    }
}

auto create_framebuffer(drm::device& device, const drm::dumb_buffer& buffer)
    -> pwm::raii<drm::framebuffer> {
    auto framebuffer = require_value(device.create_framebuffer(buffer));
    return pwm::raii{std::move(framebuffer)};
}

auto create_mode_blob(drm::device& device, const drm::display_mode& mode)
    -> pwm::raii<drm::property_blob> {
    auto blob = require_value(device.create_property_blob(mode.bytes()));
    return pwm::raii{std::move(blob)};
}

auto add_required_property(drm::atomic_request& request, pwm::u32 object_id, pwm::u32 property_id,
                           pwm::u64 value) -> void {
    if (!request.add_property(object_id, property_id, value)) {
        pwm::terminate_with_message("drmModeAtomicAddProperty failed");
    }
}

auto add_crtc_and_connector_properties(drm::device& device, const output_selection& output,
                                       const drm::property_blob& blob, drm::atomic_request& request)
    -> void {
    auto crtc_properties = pwm::raii{device.get_crtc_properties(output.crtc_id)};
    auto connector_properties = pwm::raii{device.get_connector_properties(output.connector_id)};

    const auto crtc_ids =
        drm::lookup_property_ids<drm::prop::id::crtc>(device, crtc_properties.value());
    const auto connector_ids =
        drm::lookup_property_ids<drm::prop::id::connector>(device, connector_properties.value());

    add_required_property(request, output.crtc_id, crtc_ids.active, 1);
    add_required_property(request, output.crtc_id, crtc_ids.mode_id, blob.id());
    add_required_property(request, output.connector_id, connector_ids.crtc_id, output.crtc_id);
}

auto add_plane_properties(drm::device& device, const output_selection& output,
                          const drm::framebuffer& framebuffer, drm::atomic_request& request)
    -> void {
    auto planes = pwm::raii{device.get_plane_resources()};
    if (!planes->valid() || planes->plane_ids().empty()) {
        pwm::terminate_with_message("DRM device has no usable plane");
    }

    const pwm::u32 plane_id = planes->plane_ids().front();
    auto plane_properties = pwm::raii{device.get_plane_properties(plane_id)};
    const auto plane_ids =
        drm::lookup_property_ids<drm::prop::id::plane>(device, plane_properties.value());

    add_required_property(request, plane_id, plane_ids.crtc_id, output.crtc_id);
    add_required_property(request, plane_id, plane_ids.fb_id, framebuffer.id());
    add_required_property(request, plane_id, plane_ids.crtc_x, 0);
    add_required_property(request, plane_id, plane_ids.crtc_y, 0);
    add_required_property(request, plane_id, plane_ids.crtc_w, output.width_px);
    add_required_property(request, plane_id, plane_ids.crtc_h, output.height_px);
    add_required_property(request, plane_id, plane_ids.src_x, 0);
    add_required_property(request, plane_id, plane_ids.src_y, 0);
    add_required_property(request, plane_id, plane_ids.src_w, output.width_px << 16);
    add_required_property(request, plane_id, plane_ids.src_h, output.height_px << 16);
}

auto commit_test_frame(drm::device& device, const output_selection& output,
                       const drm::framebuffer& framebuffer, const drm::property_blob& blob)
    -> void {
    auto request = pwm::raii{require_value(drm::atomic_request::create())};
    add_crtc_and_connector_properties(device, output, blob, request.value());
    add_plane_properties(device, output, framebuffer, request.value());
    require_value(device.commit_atomic(request.value(), drm::atomic_allow_modeset));
}

auto run_drm_bringup() -> int {
    auto device = open_drm_device();
    enable_atomic_features(device.value());

    const output_selection output = select_output(device.value());
    const drm::buffer_format format = make_buffer_format(output);
    auto buffer = create_buffer(device.value(), format);
    auto memory = map_buffer(buffer.value());
    fill_test_pattern(memory.value(), buffer.value().format());

    auto framebuffer = create_framebuffer(device.value(), buffer.value());
    auto blob = create_mode_blob(device.value(), output.mode);

    commit_test_frame(device.value(), output, framebuffer.value(), blob.value());
    std::this_thread::sleep_for(3s);
    require_value(device->drop_master());
    return EXIT_SUCCESS;
}

}

int main() {
    return run_drm_bringup();
}