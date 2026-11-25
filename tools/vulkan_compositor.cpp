// SPDX-License-Identifier: Apache-2.0

#include <base/fatal.hpp>
#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/drm/device.hpp>
#include <platform/drm/lookup.hpp>
#include <platform/drm/properties.hpp>
#include <platform/event/event_loop.hpp>
#include <platform/event/timer_fd.hpp>
#include <platform/seat/seat.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/instance.hpp>
#include <render/vulkanpp/scene_renderer.hpp>
#include <render/vulkanpp/texture.hpp>
#include <scene/scene.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace drm = pwm::platform::drm;
namespace event = pwm::platform::event;
namespace seat = pwm::platform::seat;
namespace vk = pwm::vulkanpp;
namespace render = pwm::render;
namespace scene = pwm::scene;

constexpr pwm::u32 buffer_count = 2;
constexpr pwm::u32 surface_count = 24;

struct output_properties {
    drm::prop::id::crtc crtc;
    drm::prop::id::connector connector;
    drm::prop::id::plane plane;
};

struct compositor_state {
    drm::device* device = nullptr;
    seat::session* session = nullptr;
    const drm::output_target* output = nullptr;
    const output_properties* properties = nullptr;
    const drm::property_blob* mode_blob = nullptr;
    std::array<const drm::dma_buf_framebuffer*, buffer_count>* framebuffers = nullptr;
    render::scene_renderer* renderer = nullptr;
    scene::scene* graph = nullptr;
    std::array<scene::node, surface_count>* surfaces = nullptr;
    std::array<pwm::base::region, buffer_count>* pending = nullptr;
    std::array<bool, buffer_count>* needs_full = nullptr;
    std::array<const vk::dma_buf_image*, buffer_count>* images = nullptr;
    std::span<const render::texture_binding> textures;
    event::timer_fd* timer = nullptr;
    bool session_enabled = false;
    bool waiting_for_flip = false;
    bool allow_modeset = true;
    bool running = true;
    pwm::u32 frame = 0;
    pwm::u32 frame_count = 180;
    pwm::u32 waiting_ticks = 0;
    pwm::u32 commit_attempts = 0;
    pwm::u32 next_buffer = 0;
    pwm::f64 phase = 0.0;
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

auto load_spirv(const std::string& path) -> pwm::result<std::vector<std::byte>> {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot open SPIR-V file"));
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "empty SPIR-V file"));
    }
    std::vector<std::byte> data(static_cast<pwm::usize>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    if (!file) {
        return std::unexpected(pwm::make_error(pwm::error_code::io, "cannot read SPIR-V file"));
    }
    return data;
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

struct drm_runtime {
    pwm::raii<seat::session> session;
    pwm::raii<seat::seat_device> seat_device;
    pwm::raii<drm::device> device;
    bool has_seat = false;
};

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

auto enable_atomic_features(drm::device& device, bool acquire_master) -> void {
    if (acquire_master && !device.is_master()) {
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

auto drm_format_for(vk::dma_buf_pixel_format format) -> pwm::u32 {
    return format == vk::dma_buf_pixel_format::rgba8888 ? drm::format_abgr8888
                                                        : drm::format_xrgb8888;
}

auto add_output_properties(const compositor_state& state,
                           const drm::dma_buf_framebuffer& framebuffer,
                           drm::atomic_request& request) -> void {
    const auto& output = *state.output;
    const auto& ids = *state.properties;
    add_required_property(request, output.crtc_id, ids.crtc.active, 1);
    add_required_property(request, output.crtc_id, ids.crtc.mode_id, state.mode_blob->id());
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

auto present_frame(compositor_state& state, pwm::u32 index) -> pwm::result<void> {
    auto request = drm::atomic_request::create();
    if (!request) {
        return std::unexpected(request.error());
    }
    auto owned_request = pwm::raii{std::move(*request)};
    add_output_properties(state, *(*state.framebuffers)[index], owned_request.value());

    pwm::u32 flags = drm::atomic_page_flip_event | drm::atomic_nonblock;
    if (state.allow_modeset) {
        flags |= drm::atomic_allow_modeset;
    }
    auto committed = state.device->commit_atomic(owned_request.value(), flags);
    if (!committed) {
        return std::unexpected(committed.error());
    }
    state.allow_modeset = false;
    state.waiting_for_flip = true;
    state.waiting_ticks = 0;
    return {};
}

auto surface_bounds(pwm::usize index, pwm::f64 phase) -> pwm::base::recti {
    constexpr pwm::i32 width = 260;
    constexpr pwm::i32 height = 150;
    const pwm::i32 column = static_cast<pwm::i32>(index % 4);
    const pwm::i32 row = static_cast<pwm::i32>(index / 4);
    const auto wobble =
        static_cast<pwm::i32>(std::sin(phase + static_cast<pwm::f64>(index)) * 18.0);
    return {40 + column * 280 + wobble, 40 + row * 180, width, height};
}

auto advance_scene(compositor_state& state) -> void {
    if (state.surfaces == nullptr) {
        return;
    }
    state.phase += 0.02;
    auto transaction = state.graph->begin();
    for (pwm::usize index = 0; index < state.surfaces->size(); ++index) {
        transaction.move((*state.surfaces)[index], surface_bounds(index, state.phase));
    }
    state.graph->add_damage(transaction.commit());
}

auto render_next_frame(compositor_state& state) -> pwm::result<void> {
    const pwm::u32 index = state.next_buffer;
    const pwm::base::region damage =
        (*state.needs_full)[index] ? pwm::base::region{} : (*state.pending)[index];
    const auto& output = *state.output;

    if (auto rendered = state.renderer->render(
            (*state.images)[index]->native_handle(), output.mode.width_px(),
            output.mode.height_px(), state.graph->root(), state.textures, damage,
            {.red = 0.03F, .green = 0.04F, .blue = 0.06F, .alpha = 1.0F});
        !rendered) {
        return std::unexpected(rendered.error());
    }
    (*state.pending)[index].clear();
    (*state.needs_full)[index] = false;

    if (auto presented = present_frame(state, index); !presented) {
        (*state.needs_full)[index] = true;
        return std::unexpected(presented.error());
    }
    state.next_buffer = (index + 1) % buffer_count;
    return {};
}

auto on_page_flip(void* context) -> void {
    auto& state = *static_cast<compositor_state*>(context);
    state.waiting_for_flip = false;
    state.waiting_ticks = 0;
}

auto on_drm_event(void* context, event::poll_event) -> void {
    auto& state = *static_cast<compositor_state*>(context);
    auto handled = state.device->handle_events(on_page_flip, &state);
    if (!handled) {
        std::println(stderr, "drm error: {}", handled.error().message);
        state.running = false;
    }
}

auto reset_after_session_change(compositor_state& state, bool enabled) -> void {
    state.session_enabled = enabled;
    state.waiting_for_flip = false;
    state.waiting_ticks = 0;
    state.commit_attempts = 0;
    if (enabled) {
        state.allow_modeset = true;
        for (bool& flag : *state.needs_full) {
            flag = true;
        }
    }
}

auto on_timer(void* context, event::poll_event) -> void {
    auto& state = *static_cast<compositor_state*>(context);
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
            reset_after_session_change(state, enabled);
            std::println("seat enabled={}", enabled);
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

    advance_scene(state);
    const auto damage = state.graph->collect_damage();
    for (auto& pending : *state.pending) {
        for (const pwm::base::recti rect : damage.rects()) {
            pending.add(rect);
        }
    }

    auto rendered = render_next_frame(state);
    if (!rendered) {
        ++state.commit_attempts;
        state.waiting_for_flip = false;
        state.allow_modeset = true;
        if (state.session != nullptr) {
            auto dispatched = state.session->dispatch(0);
            if (!dispatched) {
                std::println(stderr, "seat error: {}", dispatched.error().message);
                state.running = false;
                return;
            }
            if (!state.session->is_enabled()) {
                reset_after_session_change(state, false);
                return;
            }
        }
        if (state.commit_attempts > 120) {
            std::println(stderr, "present error: {} (system={})", rendered.error().message,
                         rendered.error().system_error);
            state.running = false;
        }
        return;
    }
    state.commit_attempts = 0;
    ++state.frame;
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

auto run_loop(compositor_state& state) -> pwm::result<void> {
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

struct vulkan_runtime {
    pwm::raii<vk::instance> instance;
    pwm::raii<vk::device> device;
    std::array<pwm::raii<vk::dma_buf_image>, buffer_count> images;
    pwm::raii<vk::texture> texture;
    pwm::raii<render::scene_renderer> renderer;
    std::array<render::texture_binding, 1> bindings{};
};

auto make_scene(scene::scene& graph, scene::node& background,
                std::array<scene::node, surface_count>& surfaces, pwm::u32 width_px,
                pwm::u32 height_px) -> void {
    background = scene::node{1};
    background.set_bounds(
        {0, 0, static_cast<pwm::i32>(width_px), static_cast<pwm::i32>(height_px)});
    background.set_style({.fill = {.red = 0.03F, .green = 0.04F, .blue = 0.06F, .alpha = 1.0F}});
    graph.root().add_child(background);

    const std::array<scene::color, 4> colors{{
        {.red = 0.20F, .green = 0.55F, .blue = 0.95F, .alpha = 1.0F},
        {.red = 0.95F, .green = 0.35F, .blue = 0.40F, .alpha = 1.0F},
        {.red = 0.30F, .green = 0.85F, .blue = 0.45F, .alpha = 1.0F},
        {.red = 0.95F, .green = 0.75F, .blue = 0.25F, .alpha = 1.0F},
    }};
    for (pwm::usize index = 0; index < surfaces.size(); ++index) {
        auto& item = surfaces[index];
        item = scene::node{static_cast<scene::node_id>(index + 2)};
        item.set_bounds(surface_bounds(index, 0.0));
        item.set_style({.fill = colors[index % colors.size()],
                        .shadow = {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.5F},
                        .corner_radius = 18.0F,
                        .shadow_radius = 12.0F,
                        .shadow_offset_y = 6.0F});
        if (index % 3 == 0) {
            item.set_texture(1);
        }
        graph.root().add_child(item);
    }
}

auto create_checker_pixels() -> std::vector<std::byte> {
    std::vector<std::byte> pixels(64 * 64 * 4);
    for (pwm::u32 y = 0; y < 64; ++y) {
        for (pwm::u32 x = 0; x < 64; ++x) {
            const bool light = ((x / 8) + (y / 8)) % 2 == 0;
            const pwm::usize offset = (static_cast<pwm::usize>(y) * 64 + x) * 4;
            const pwm::u8 value = light ? 0xDD : 0x77;
            pixels[offset] = static_cast<std::byte>(value);
            pixels[offset + 1] = static_cast<std::byte>(value);
            pixels[offset + 2] = static_cast<std::byte>(value);
            pixels[offset + 3] = std::byte{0xFF};
        }
    }
    return pixels;
}

auto create_vulkan_runtime(vulkan_runtime& runtime, pwm::u32 width_px, pwm::u32 height_px) -> void {
    const vk::application_info application{
        .name = "powerwm_vulkan_compositor",
        .version = 1,
        .engine_name = "powerwm",
        .engine_version = 1,
        .api_version = 0,
    };
    runtime.instance = pwm::raii{require_value(vk::instance::create(application))};
    runtime.device = pwm::raii{require_value(vk::device::create(runtime.instance.value()))};
    if (!runtime.device->supports_dma_buf()) {
        pwm::terminate_with_message("Vulkan DMA-BUF export is unavailable");
    }

    for (pwm::u32 index = 0; index < buffer_count; ++index) {
        runtime.images[index] = pwm::raii{
            require_value(vk::create_dma_buf_image(runtime.device.value(), width_px, height_px))};
        if (runtime.images[index]->pixel_format() != runtime.images[0]->pixel_format()) {
            pwm::terminate_with_message("DMA-BUF pixel formats are inconsistent");
        }
    }

    const auto checker = create_checker_pixels();
    runtime.texture =
        pwm::raii{require_value(vk::create_texture(runtime.device.value(), 64, 64, checker))};

    auto vertex = require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.vert.spv"));
    auto fragment = require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.frag.spv"));
    const render::target_format format =
        runtime.images[0]->pixel_format() == vk::dma_buf_pixel_format::rgba8888
            ? render::target_format::rgba8
            : render::target_format::bgra8;
    runtime.renderer = pwm::raii{require_value(render::scene_renderer::create(
        runtime.device.value(), format, std::span<const std::byte>{vertex},
        std::span<const std::byte>{fragment}))};

    runtime.bindings[0] = {.id = 1, .texture = &runtime.texture.value()};
}

}

int main(int argc, char** argv) {
    const auto frame_count = parse_frame_count(argc, argv);
    if (!frame_count) {
        std::println(stderr, "usage: powerwm_vulkan_compositor [frame-count]");
        return EXIT_FAILURE;
    }

    auto runtime = open_drm_runtime();
    auto& device = runtime.device.value();
    seat::session* session = runtime.has_seat ? &runtime.session.value() : nullptr;
    if (session != nullptr) {
        wait_until_enabled(*session);
    }
    enable_atomic_features(device, session == nullptr);

    const drm::output_target output = require_value(device.find_primary_output());
    const output_properties ids = lookup_output_properties(device, output);
    auto blob = pwm::raii{require_value(device.create_property_blob(output.mode.bytes()))};

    vulkan_runtime graphics{};
    create_vulkan_runtime(graphics, output.mode.width_px(), output.mode.height_px());

    std::array<pwm::raii<drm::dma_buf_framebuffer>, buffer_count> framebuffers;
    std::array<const drm::dma_buf_framebuffer*, buffer_count> framebuffer_ptrs{};
    std::array<const vk::dma_buf_image*, buffer_count> image_ptrs{};
    for (pwm::u32 index = 0; index < buffer_count; ++index) {
        const auto& image = graphics.images[index].value();
        framebuffers[index] = pwm::raii{require_value(device.create_framebuffer_from_dma_buf(
            image.fd(), image.width_px(), image.height_px(), image.pitch_bytes(),
            drm_format_for(image.pixel_format()), image.modifier()))};
        framebuffer_ptrs[index] = &framebuffers[index].value();
        image_ptrs[index] = &image;
    }

    scene::scene graph;
    scene::node background;
    std::array<scene::node, surface_count> surfaces;
    make_scene(graph, background, surfaces, output.mode.width_px(), output.mode.height_px());

    std::array<pwm::base::region, buffer_count> pending{};
    std::array<bool, buffer_count> needs_full{true, true};
    auto timer = pwm::raii{require_value(event::timer_fd::create())};

    compositor_state state{};
    state.device = &device;
    state.session = session;
    state.output = &output;
    state.properties = &ids;
    state.mode_blob = &blob.value();
    state.framebuffers = &framebuffer_ptrs;
    state.renderer = &graphics.renderer.value();
    state.graph = &graph;
    state.surfaces = &surfaces;
    state.pending = &pending;
    state.needs_full = &needs_full;
    state.images = &image_ptrs;
    state.textures = graphics.bindings;
    state.timer = &timer.value();
    state.session_enabled = session == nullptr || session->is_enabled();
    state.frame_count = *frame_count;

    require_value(run_loop(state));
    std::println("presented {} frames", state.frame);

    if (session == nullptr && device.is_master()) {
        require_value(device.drop_master());
    }
    return EXIT_SUCCESS;
}
