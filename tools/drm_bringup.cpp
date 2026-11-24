// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>
#include <platform/seat/seat.hpp>

#include <cstdlib>
#include <filesystem>
#include <print>
#include <string>
#include <system_error>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;
namespace seat = pwm::platform::seat;

struct drm_runtime {
    pwm::raii<seat::session> session;
    pwm::raii<seat::seat_device> seat_device;
    pwm::raii<drm::device> device;
    bool has_seat = false;
};

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

auto find_first_card_device() -> std::string {
    std::error_code error;
    std::filesystem::directory_iterator iterator{"/dev/dri", error};
    if (error) {
        return {};
    }
    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with("card")) {
            return entry.path().string();
        }
    }
    return {};
}

auto open_drm_runtime() -> drm_runtime {
    auto session = seat::session::create();
    if (session) {
        auto session_handle = pwm::raii{std::move(*session)};
        const std::string card = find_first_card_device();
        if (!card.empty()) {
            auto seat_device = session_handle->open_device(card);
            if (seat_device) {
                const pwm::handle_t fd = seat_device->fd();
                auto device = pwm::raii{drm::device::create_borrowed(fd)};
                return drm_runtime{
                    .session = std::move(session_handle),
                    .seat_device = pwm::raii{std::move(*seat_device)},
                    .device = std::move(device),
                    .has_seat = true,
                };
            }
        }
        std::println(stderr, "warning: libseat device open failed; using direct DRM");
    } else {
        std::println(stderr, "warning: libseat unavailable; using direct DRM");
    }

    auto device = pwm::raii{require_value(drm::device::open_first_available())};
    return drm_runtime{
        .session = {},
        .seat_device = {},
        .device = std::move(device),
        .has_seat = false,
    };
}

auto wait_until_enabled(seat::session& session) -> void {
    for (pwm::i32 attempt = 0; attempt < 50 && !session.is_enabled(); ++attempt) {
        require_value(session.dispatch(100));
    }
    if (!session.is_enabled()) {
        pwm::terminate_with_message("seat did not become enabled");
    }
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

auto wait_for_page_flip(drm::device& device, seat::session* session) -> pwm::result<bool> {
    if (session == nullptr) {
        auto result = device.wait_for_page_flip(1000);
        if (!result) {
            return std::unexpected(result.error());
        }
        return true;
    }

    constexpr pwm::i32 attempts = 20;
    for (pwm::i32 attempt = 0; attempt < attempts; ++attempt) {
        auto result = device.wait_for_page_flip(50);
        if (result) {
            return true;
        }
        auto dispatched = session->dispatch(0);
        if (!dispatched) {
            return std::unexpected(dispatched.error());
        }
        if (!session->is_enabled()) {
            return false;
        }
    }
    return std::unexpected(pwm::make_error(pwm::error_code::io, "page flip timed out"));
}

auto render_frames(drm::device& device, seat::session* session, const drm::output_target& output,
                   const output_properties& ids, const drm::property_blob& blob,
                   const drm::framebuffer& first, const drm::framebuffer& second,
                   drm::mapped_memory& first_memory, drm::mapped_memory& second_memory,
                   const drm::buffer_format& format) -> void {
    constexpr pwm::u32 frame_count = 180;
    pwm::u32 frame = 0;
    bool allow_modeset = true;

    while (frame < frame_count) {
        if (session != nullptr && !session->is_enabled()) {
            wait_until_enabled(*session);
            require_value(device.set_master());
            allow_modeset = true;
        }

        const bool use_first = frame % 2 == 0;
        auto& memory = use_first ? first_memory : second_memory;
        const auto& framebuffer = use_first ? first : second;
        fill_animation_frame(memory, format, frame);
        commit_frame(device, output, ids, framebuffer, blob, allow_modeset);
        allow_modeset = false;

        const bool completed = require_value(wait_for_page_flip(device, session));
        if (!completed) {
            allow_modeset = true;
            continue;
        }
        ++frame;
    }
}

auto run_drm_bringup() -> int {
    auto runtime = open_drm_runtime();
    auto& device = runtime.device.value();
    seat::session* session = runtime.has_seat ? &runtime.session.value() : nullptr;

    if (session != nullptr) {
        wait_until_enabled(*session);
    }
    enable_atomic_features(device);

    const drm::output_target output = require_value(device.find_primary_output());
    const drm::buffer_format format = make_buffer_format(output);

    auto first_buffer = create_buffer(device, format);
    auto second_buffer = create_buffer(device, format);
    auto first_memory = map_buffer(first_buffer.value());
    auto second_memory = map_buffer(second_buffer.value());
    auto first_framebuffer = create_framebuffer(device, first_buffer.value());
    auto second_framebuffer = create_framebuffer(device, second_buffer.value());
    auto blob = create_mode_blob(device, output.mode);
    const output_properties ids = lookup_output_properties(device, output);

    render_frames(device, session, output, ids, blob.value(), first_framebuffer.value(),
                  second_framebuffer.value(), first_memory.value(), second_memory.value(),
                  first_buffer.value().format());

    if (session == nullptr || session->is_enabled()) {
        require_value(device.drop_master());
    }
    return EXIT_SUCCESS;
}

}

int main() {
    return run_drm_bringup();
}