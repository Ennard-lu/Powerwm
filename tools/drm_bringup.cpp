// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>

#include <cstdlib>
#include <print>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;

struct output_properties {
    drm::prop::id::crtc crtc;
    drm::prop::id::connector connector;
    drm::prop::id::plane plane;
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
    auto device = require_value(drm::device::open_first_available());
    return pwm::raii{std::move(device)};
}

auto enable_atomic_features(drm::device& device) -> void {
    require_value(device.set_master());
    require_value(device.set_atomic_enabled(true));
    require_value(device.set_universal_planes_enabled(true));
}

auto make_buffer_format(const drm::output_target& output) -> drm::buffer_format {
    return {
        .width_px = output.mode.width_px(),
        .height_px = output.mode.height_px(),
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

auto fill_animation_frame(drm::mapped_memory& memory, const drm::buffer_format& format,
                          pwm::u32 frame) -> void {
    auto* pixels = static_cast<pwm::u32*>(memory.address);
    const pwm::usize row_stride = format.pitch_bytes / sizeof(pwm::u32);

    for (pwm::u32 y = 0; y < format.height_px; ++y) {
        auto* row = pixels + static_cast<pwm::usize>(y) * row_stride;
        const pwm::u32 green = (y + frame * 4) & 0xFF;
        for (pwm::u32 x = 0; x < format.width_px; ++x) {
            const pwm::u32 red = (x + frame * 2) & 0xFF;
            row[x] = (red << 16) | (green << 8) | 0x40;
        }
    }
}

auto add_required_property(drm::atomic_request& request, pwm::u32 object_id, pwm::u32 property_id,
                           pwm::u64 value) -> void {
    if (!request.add_property(object_id, property_id, value)) {
        pwm::terminate_with_message("drmModeAtomicAddProperty failed");
    }
}

auto add_crtc_and_connector_properties(const drm::output_target& output,
                                       const output_properties& ids, const drm::property_blob& blob,
                                       drm::atomic_request& request) -> void {
    add_required_property(request, output.crtc_id, ids.crtc.active, 1);
    add_required_property(request, output.crtc_id, ids.crtc.mode_id, blob.id());
    add_required_property(request, output.connector_id, ids.connector.crtc_id, output.crtc_id);
}

auto add_plane_properties(const drm::output_target& output, const output_properties& ids,
                          const drm::framebuffer& framebuffer, drm::atomic_request& request)
    -> void {
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

auto commit_frame(drm::device& device, const drm::output_target& output,
                  const output_properties& ids, const drm::framebuffer& framebuffer,
                  const drm::property_blob& blob, bool allow_modeset) -> void {
    auto request = pwm::raii{require_value(drm::atomic_request::create())};
    add_crtc_and_connector_properties(output, ids, blob, request.value());
    add_plane_properties(output, ids, framebuffer, request.value());

    pwm::u32 flags = drm::atomic_page_flip_event | drm::atomic_nonblock;
    if (allow_modeset) {
        flags |= drm::atomic_allow_modeset;
    }
    require_value(device.commit_atomic(request.value(), flags));
}

auto render_frames(drm::device& device, const drm::output_target& output,
                   const output_properties& ids, const drm::property_blob& blob,
                   const drm::framebuffer& first, const drm::framebuffer& second,
                   drm::mapped_memory& first_memory, drm::mapped_memory& second_memory,
                   const drm::buffer_format& format) -> void {
    constexpr pwm::u32 frame_count = 180;

    fill_animation_frame(first_memory, format, 0);
    commit_frame(device, output, ids, first, blob, true);
    require_value(device.wait_for_page_flip(1000));

    for (pwm::u32 frame = 0; frame < frame_count; ++frame) {
        const bool use_first = frame % 2 != 0;
        auto& memory = use_first ? first_memory : second_memory;
        const auto& framebuffer = use_first ? first : second;
        fill_animation_frame(memory, format, frame + 1);
        commit_frame(device, output, ids, framebuffer, blob, false);
        require_value(device.wait_for_page_flip(1000));
    }
}

auto run_drm_bringup() -> int {
    auto device = open_drm_device();
    enable_atomic_features(device.value());

    const drm::output_target output = require_value(device->find_primary_output());
    const drm::buffer_format format = make_buffer_format(output);

    auto first_buffer = create_buffer(device.value(), format);
    auto second_buffer = create_buffer(device.value(), format);
    auto first_memory = map_buffer(first_buffer.value());
    auto second_memory = map_buffer(second_buffer.value());
    auto first_framebuffer = create_framebuffer(device.value(), first_buffer.value());
    auto second_framebuffer = create_framebuffer(device.value(), second_buffer.value());
    auto blob = create_mode_blob(device.value(), output.mode);
    const output_properties ids = lookup_output_properties(device.value(), output);

    render_frames(device.value(), output, ids, blob.value(), first_framebuffer.value(),
                  second_framebuffer.value(), first_memory.value(), second_memory.value(),
                  first_buffer.value().format());

    require_value(device->drop_master());
    return EXIT_SUCCESS;
}

}

int main() {
    return run_drm_bringup();
}