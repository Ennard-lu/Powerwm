// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>
#include <platform/event/event_loop.hpp>
#include <platform/event/timer_fd.hpp>
#include <platform/input/context.hpp>
#include <platform/seat/seat.hpp>
#include <platform/udev/monitor.hpp>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

namespace drm = pwm::platform::drm;
namespace event = pwm::platform::event;
namespace input = pwm::platform::input;
namespace seat = pwm::platform::seat;
namespace udev = pwm::platform::udev;

struct drm_runtime {
    pwm::raii<seat::session> session;
    pwm::raii<seat::seat_device> seat_device;
    pwm::raii<drm::device> device;
    bool has_seat = false;
};

struct optional_sources {
    pwm::raii<input::context> input;
    pwm::raii<udev::monitor> udev_monitor;
    bool has_input = false;
    bool has_udev = false;
};

struct output_properties {
    drm::prop::id::crtc crtc;
    drm::prop::id::connector connector;
    drm::prop::id::plane plane;
};

enum class run_result {
    success,
    restart,
    failure,
};

struct demo_state {
    drm::device* device = nullptr;
    seat::session* session = nullptr;
    udev::monitor* udev_monitor = nullptr;
    input::context* input = nullptr;
    const drm::output_target* output = nullptr;
    const output_properties* properties = nullptr;
    const drm::property_blob* mode_blob = nullptr;
    const drm::framebuffer* first_framebuffer = nullptr;
    const drm::framebuffer* second_framebuffer = nullptr;
    drm::mapped_memory* first_memory = nullptr;
    drm::mapped_memory* second_memory = nullptr;
    const drm::buffer_format* format = nullptr;
    event::timer_fd* timer = nullptr;
    bool session_enabled = false;
    bool waiting_for_flip = false;
    bool allow_modeset = true;
    bool running = true;
    bool restart_requested = false;
    pwm::u32 frame = 0;
    pwm::u32 frame_count = 180;
    pwm::u32 waiting_ticks = 0;
    pwm::u32 commit_attempts = 0;
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

auto create_optional_sources() -> optional_sources {
    optional_sources sources;

    auto input_result = input::context::create("seat0");
    if (input_result) {
        sources.input = pwm::raii{std::move(*input_result)};
        sources.has_input = true;
    } else {
        std::println(stderr, "warning: libinput unavailable");
    }

    auto udev_result = udev::monitor::create();
    if (udev_result) {
        sources.udev_monitor = pwm::raii{std::move(*udev_result)};
        sources.has_udev = true;
    } else {
        std::println(stderr, "warning: udev monitor unavailable");
    }

    return sources;
}

auto enable_atomic_features(drm::device& device, bool acquire_master) -> void {
    if (acquire_master && !device.is_master()) {
        require_value(device.set_master());
    }
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
    const pwm::i32 result = request.add_property(object_id, property_id, value);
    if (result != 0) {
        std::println(stderr, "property add failed object={} property={} value={} result={}",
                     object_id, property_id, value, result);
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
                  const drm::property_blob& blob, bool allow_modeset) -> pwm::result<void> {
    auto request = drm::atomic_request::create();
    if (!request) {
        return std::unexpected(request.error());
    }
    auto owned_request = pwm::raii{std::move(*request)};
    add_crtc_and_connector_properties(output, ids, blob, owned_request.value());
    add_plane_properties(output, ids, framebuffer, owned_request.value());

    pwm::u32 flags = drm::atomic_page_flip_event | drm::atomic_nonblock;
    if (allow_modeset) {
        flags |= drm::atomic_allow_modeset;
    }
    return device.commit_atomic(owned_request.value(), flags);
}

auto print_event(const input::input_event& input_event) -> void {
    using input::input_event_type;
    switch (input_event.type) {
    case input_event_type::keyboard_key:
        std::println("key code={} state={}", input_event.key_code,
                     input_event.pressed ? "pressed" : "released");
        break;
    case input_event_type::pointer_motion:
        std::println("motion dx={:.2f} dy={:.2f}", input_event.dx, input_event.dy);
        break;
    case input_event_type::pointer_button:
        std::println("button code={} state={}", input_event.button,
                     input_event.pressed ? "pressed" : "released");
        break;
    case input_event_type::pointer_axis:
        std::println("axis value={:.2f}", input_event.axis_value);
        break;
    case input_event_type::touch_down:
    case input_event_type::touch_up:
    case input_event_type::touch_motion:
        std::println("touch slot={} x={:.2f} y={:.2f}", input_event.touch_slot, input_event.x,
                     input_event.y);
        break;
    default:
        break;
    }
}

auto present_frame(demo_state& state) -> pwm::result<void> {
    const bool use_first = state.frame % 2 == 0;
    auto& memory = use_first ? *state.first_memory : *state.second_memory;
    const auto& framebuffer = use_first ? *state.first_framebuffer : *state.second_framebuffer;

    fill_animation_frame(memory, *state.format, state.frame);
    auto committed = commit_frame(*state.device, *state.output, *state.properties, framebuffer,
                                  *state.mode_blob, state.allow_modeset);
    if (!committed) {
        return std::unexpected(committed.error());
    }
    state.allow_modeset = false;
    state.waiting_for_flip = true;
    state.waiting_ticks = 0;
    ++state.frame;
    return {};
}

auto on_page_flip(void* context) -> void {
    auto& state = *static_cast<demo_state*>(context);
    state.waiting_for_flip = false;
    state.waiting_ticks = 0;
}

auto on_drm_event(void* context, event::poll_event) -> void {
    auto& state = *static_cast<demo_state*>(context);
    auto handled = state.device->handle_events(on_page_flip, &state);
    if (!handled) {
        std::println(stderr, "drm error: {}", handled.error().message);
        state.running = false;
    }
}

auto on_udev_event(void* context, event::poll_event) -> void {
    auto& state = *static_cast<demo_state*>(context);
    if (auto device_event = state.udev_monitor->receive()) {
        std::println("udev action={} subsystem={} devnode={} hotplug={}", device_event->action,
                     device_event->subsystem, device_event->devnode, device_event->hotplug);
        if (device_event->hotplug || device_event->action == "add" ||
            device_event->action == "remove") {
            state.restart_requested = true;
            state.running = false;
        }
    }
}

auto on_input_event(void* context, event::poll_event) -> void {
    auto& state = *static_cast<demo_state*>(context);
    auto dispatched = state.input->dispatch();
    if (!dispatched) {
        std::println(stderr, "input error: {}", dispatched.error().message);
        state.running = false;
        return;
    }
    while (auto input_event = state.input->next_event()) {
        print_event(*input_event);
    }
}

auto on_timer(void* context, event::poll_event) -> void {
    auto& state = *static_cast<demo_state*>(context);
    auto expirations = state.timer->read_expirations();
    if (!expirations) {
        std::println(stderr, "timer error: {}", expirations.error().message);
        state.running = false;
        return;
    }

    if (state.session != nullptr) {
        auto dispatched = state.session->dispatch(0);
        if (!dispatched) {
            std::println(stderr, "seat error: {}", dispatched.error().message);
            state.running = false;
            return;
        }
        const bool enabled = state.session->is_enabled();
        if (enabled != state.session_enabled) {
            state.session_enabled = enabled;
            state.waiting_for_flip = false;
            state.waiting_ticks = 0;
            state.commit_attempts = 0;
            std::println("seat enabled={}", enabled);
            if (enabled) {
                state.allow_modeset = true;
            }
        }
    }

    if (!state.session_enabled) {
        return;
    }
    if (state.waiting_for_flip) {
        ++state.waiting_ticks;
        if (state.waiting_ticks > 60) {
            std::println(stderr, "page flip timed out");
            state.running = false;
        }
        return;
    }
    if (state.frame >= state.frame_count) {
        state.running = false;
        return;
    }
    auto presented = present_frame(state);
    if (!presented) {
        ++state.commit_attempts;
        state.allow_modeset = true;
        if (state.commit_attempts > 120) {
            std::println(stderr, "commit error: {} (system={})", presented.error().message,
                         presented.error().system_error);
            state.running = false;
        }
        return;
    }
    state.commit_attempts = 0;
}

auto parse_frame_count(int argc, char** argv) -> std::optional<pwm::u32> {
    if (argc == 1) {
        return 180;
    }
    if (argc != 2) {
        return std::nullopt;
    }

    const std::string_view text{argv[1]};
    pwm::u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

auto run_demo_loop(demo_state& state) -> pwm::result<void> {
    event::event_loop loop;
    if (auto added =
            loop.add(state.device->fd(), event::poll_event::readable, on_drm_event, &state);
        !added) {
        return std::unexpected(added.error());
    }
    if (auto added = loop.add(state.timer->fd(), event::poll_event::readable, on_timer, &state);
        !added) {
        return std::unexpected(added.error());
    }
    if (state.input != nullptr) {
        if (auto added =
                loop.add(state.input->fd(), event::poll_event::readable, on_input_event, &state);
            !added) {
            return std::unexpected(added.error());
        }
    }
    if (state.udev_monitor != nullptr) {
        if (auto added = loop.add(state.udev_monitor->fd(), event::poll_event::readable,
                                  on_udev_event, &state);
            !added) {
            return std::unexpected(added.error());
        }
    }
    if (auto armed = state.timer->arm_ms(16); !armed) {
        return std::unexpected(armed.error());
    }

    while (state.running) {
        auto dispatched = loop.run_once(100);
        if (!dispatched) {
            return std::unexpected(dispatched.error());
        }
    }
    return {};
}

auto run_drm_bringup(pwm::u32 frame_count) -> run_result {
    auto runtime = open_drm_runtime();
    auto& device = runtime.device.value();
    seat::session* session = runtime.has_seat ? &runtime.session.value() : nullptr;

    if (session != nullptr) {
        wait_until_enabled(*session);
    }
    enable_atomic_features(device, session == nullptr);

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

    auto sources = create_optional_sources();
    auto timer = pwm::raii{require_value(event::timer_fd::create())};
    demo_state state{
        .device = &device,
        .session = session,
        .udev_monitor = sources.has_udev ? &sources.udev_monitor.value() : nullptr,
        .input = sources.has_input ? &sources.input.value() : nullptr,
        .output = &output,
        .properties = &ids,
        .mode_blob = &blob.value(),
        .first_framebuffer = &first_framebuffer.value(),
        .second_framebuffer = &second_framebuffer.value(),
        .first_memory = &first_memory.value(),
        .second_memory = &second_memory.value(),
        .format = &format,
        .timer = &timer.value(),
        .session_enabled = session == nullptr || session->is_enabled(),
        .waiting_for_flip = false,
        .allow_modeset = true,
        .running = true,
        .restart_requested = false,
        .frame = 0,
        .frame_count = frame_count,
        .waiting_ticks = 0,
        .commit_attempts = 0,
    };

    require_value(run_demo_loop(state));

    std::println("presented {} frames", state.frame);

    if (session == nullptr && device.is_master()) {
        require_value(device.drop_master());
    }
    return state.restart_requested ? run_result::restart : run_result::success;
}

}

int main(int argc, char** argv) {
    const auto frame_count = parse_frame_count(argc, argv);
    if (!frame_count) {
        std::println(stderr, "usage: powerwm_drm_bringup [frame-count]");
        return EXIT_FAILURE;
    }

    while (true) {
        const run_result result = run_drm_bringup(*frame_count);
        if (result == run_result::restart) {
            continue;
        }
        return result == run_result::success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
}