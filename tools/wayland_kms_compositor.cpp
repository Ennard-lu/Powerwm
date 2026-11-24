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
#include <platform/wayland/socket.hpp>
#include <render/vulkanpp/device.hpp>
#include <render/vulkanpp/dma_buf_image.hpp>
#include <render/vulkanpp/instance.hpp>
#include <render/vulkanpp/scene_renderer.hpp>
#include <scene/scene.hpp>
#include <wayland/connection.hpp>
#include <wayland/display.hpp>
#include <wayland/surface_scene.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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
namespace wayland = pwm::wayland;
namespace transport = pwm::platform::wayland;
namespace input = pwm::platform::input;

constexpr pwm::u32 buffer_count = 2;

struct output_properties {
    drm::prop::id::crtc crtc;
    drm::prop::id::connector connector;
    drm::prop::id::plane plane;
};

struct compositor_state;
struct client_session;

struct pending_feedback {
    pwm::u32 client = 0;
    wayland::object_id feedback = 0;
};

struct clipboard_source {
    pwm::u32 client = 0;
    wayland::object_id source = 0;
    pwm::u32 actions = 0;
    std::vector<std::string> mime_types;
};

struct drag_source {
    pwm::u32 client = 0;
    wayland::object_id source = 0;
    pwm::u32 actions = 0;
    pwm::u32 action = 0;
    std::vector<std::string> mime_types;
    pwm::u64 focus_surface = 0;
    wayland::object_id focus_offer = 0;
    bool dropped = false;
};

struct compositor_state {
    drm::device* device = nullptr;
    seat::session* session = nullptr;
    const drm::output_target* output = nullptr;
    const output_properties* properties = nullptr;
    const drm::property_blob* mode_blob = nullptr;
    std::array<const drm::dma_buf_framebuffer*, buffer_count>* framebuffers = nullptr;
    render::scene_renderer* renderer = nullptr;
    wayland::surface_scene* scene = nullptr;
    std::array<const vk::dma_buf_image*, buffer_count>* images = nullptr;
    event::timer_fd* timer = nullptr;
    event::event_loop* loop = nullptr;
    transport::listener* listener = nullptr;
    input::context* input = nullptr;
    std::vector<std::unique_ptr<client_session>> clients;
    std::vector<pending_feedback> pending_presentations;
    std::optional<clipboard_source> clipboard;
    std::optional<drag_source> drag;
    pwm::u32 next_client = 1;
    bool session_enabled = false;
    bool waiting_for_flip = false;
    bool allow_modeset = true;
    bool running = true;
    pwm::f64 cursor_x = 0.0;
    pwm::f64 cursor_y = 0.0;
    pwm::u64 pointer_focus = 0;
    std::array<pwm::u64, 32> touch_focus{};
    pwm::u32 frame = 0;
    pwm::u32 frame_count = 180;
    pwm::u32 waiting_ticks = 0;
    pwm::u32 commit_attempts = 0;
    pwm::u32 next_buffer = 0;
};

struct client_session {
    client_session(compositor_state& compositor, pwm::u32 index, transport::stream stream) noexcept
        : compositor(&compositor), index(index), connection(std::move(stream)),
          display(connection) {
    }

    compositor_state* compositor = nullptr;
    pwm::u32 index = 0;
    wayland::connection connection;
    wayland::display_service display;
    bool closed = false;
};

auto on_presentation_feedback(void* context, const wayland::presentation_feedback_event& feedback)
    -> void {
    auto& session = *static_cast<client_session*>(context);
    session.compositor->pending_presentations.push_back(
        {.client = session.index, .feedback = feedback.feedback});
}

auto on_client_commit(void* context, const wayland::surface_commit_event& commit) -> void {
    auto& session = *static_cast<client_session*>(context);
    const pwm::u64 key = (static_cast<pwm::u64>(session.index) << 32U) | commit.surface;
    if (auto applied = session.compositor->scene->on_commit(key, commit); !applied) {
        std::println(stderr, "client {} commit error: {}", session.index, applied.error().message);
        return;
    }
    std::println("client {} surface {} committed {}x{}{}", session.index, commit.surface,
                 commit.width, commit.height, commit.is_dmabuf ? " dmabuf" : " shm");
    static_cast<void>(session.display.send_keyboard_enter(commit.surface));
    static_cast<void>(session.connection.flush());
}

auto find_client(compositor_state& state, pwm::u32 index) -> client_session*;
auto end_drag(compositor_state& state) -> void;
auto cancel_drag(compositor_state& state) -> void;

auto broadcast_selection(compositor_state& state) -> void {
    for (const auto& session : state.clients) {
        if (session->closed) {
            continue;
        }
        if (!state.clipboard) {
            static_cast<void>(session->display.send_selection(0));
            static_cast<void>(session->connection.flush());
            continue;
        }
        if (session->index == state.clipboard->client) {
            continue;
        }
        auto offer = session->display.create_data_offer(
            state.clipboard->client, state.clipboard->source, state.clipboard->mime_types,
            state.clipboard->actions);
        if (!offer) {
            continue;
        }
        static_cast<void>(session->display.send_selection(*offer));
        static_cast<void>(session->connection.flush());
    }
}

auto cancel_clipboard(compositor_state& state) -> void {
    if (!state.clipboard) {
        return;
    }
    const clipboard_source source = std::move(*state.clipboard);
    state.clipboard.reset();
    auto* client = find_client(state, source.client);
    if (client != nullptr && !client->closed) {
        static_cast<void>(client->display.send_data_source_cancelled(source.source));
        static_cast<void>(client->connection.flush());
    }
}

auto on_data_source(void* context, const wayland::data_source_event& event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto& state = *session.compositor;
    if (event.source == 0) {
        if (state.clipboard && state.clipboard->client == session.index) {
            state.clipboard.reset();
            broadcast_selection(state);
        }
        return;
    }
    if (state.clipboard &&
        (state.clipboard->client != session.index || state.clipboard->source != event.source)) {
        cancel_clipboard(state);
    }
    state.clipboard = clipboard_source{
        .client = session.index,
        .source = event.source,
        .actions = event.actions,
        .mime_types = std::vector<std::string>{event.mime_types.begin(), event.mime_types.end()},
    };
    broadcast_selection(state);
}

auto on_data_source_destroyed(void* context, wayland::object_id source) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto& state = *session.compositor;
    if (state.clipboard && state.clipboard->client == session.index &&
        state.clipboard->source == source) {
        state.clipboard.reset();
        broadcast_selection(state);
    }
    if (state.drag && state.drag->client == session.index && state.drag->source == source) {
        end_drag(state);
    }
}

auto on_data_offer_receive(void* context, const wayland::data_offer_receive_event& event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto* source = find_client(*session.compositor, event.source_client);
    if (source == nullptr || source->closed) {
        return;
    }
    static_cast<void>(
        source->display.send_data_source_send(event.source_object, event.mime_type, event.fd));
}

auto send_drag_leave(compositor_state& state) -> void {
    if (!state.drag || state.drag->focus_surface == 0) {
        return;
    }
    auto* client = find_client(state, static_cast<pwm::u32>(state.drag->focus_surface >> 32U));
    if (client != nullptr && !client->closed) {
        static_cast<void>(client->display.send_data_device_leave());
        client->display.destroy_data_offer(state.drag->focus_offer);
        static_cast<void>(client->connection.flush());
    }
    state.drag->focus_surface = 0;
    state.drag->focus_offer = 0;
}

auto end_drag(compositor_state& state) -> void {
    send_drag_leave(state);
    state.drag.reset();
}

auto cancel_drag(compositor_state& state) -> void {
    if (!state.drag) {
        return;
    }
    auto* source = find_client(state, state.drag->client);
    if (source != nullptr && !source->closed && !state.drag->dropped) {
        static_cast<void>(source->display.send_data_source_cancelled(state.drag->source));
        static_cast<void>(source->connection.flush());
    }
    end_drag(state);
}

auto update_drag_focus(compositor_state& state) -> void {
    if (!state.drag) {
        return;
    }
    const pwm::u64 focus = state.scene->hit_test(state.cursor_x, state.cursor_y).value_or(0);
    if (focus == state.drag->focus_surface) {
        return;
    }
    send_drag_leave(state);
    if (focus == 0) {
        return;
    }
    auto* client = find_client(state, static_cast<pwm::u32>(focus >> 32U));
    const auto bounds = state.scene->surface_bounds(focus);
    if (client == nullptr || client->closed || !bounds) {
        return;
    }
    auto offer = client->display.create_data_offer(state.drag->client, state.drag->source,
                                                   state.drag->mime_types, state.drag->actions);
    if (!offer) {
        return;
    }
    state.drag->focus_surface = focus;
    state.drag->focus_offer = *offer;
    static_cast<void>(client->display.send_data_offer_event(*offer));
    static_cast<void>(client->display.send_data_device_enter(
        client->display.next_serial(), static_cast<wayland::object_id>(focus),
        state.cursor_x - bounds->x, state.cursor_y - bounds->y, *offer));
    static_cast<void>(client->connection.flush());
}

auto motion_drag(compositor_state& state, pwm::u32 time_ms) -> void {
    if (!state.drag || state.drag->focus_surface == 0) {
        return;
    }
    auto* client = find_client(state, static_cast<pwm::u32>(state.drag->focus_surface >> 32U));
    const auto bounds = state.scene->surface_bounds(state.drag->focus_surface);
    if (client == nullptr || client->closed || !bounds) {
        return;
    }
    static_cast<void>(client->display.send_data_device_motion(time_ms, state.cursor_x - bounds->x,
                                                              state.cursor_y - bounds->y));
    static_cast<void>(client->connection.flush());
}

auto drop_drag(compositor_state& state) -> void {
    if (!state.drag) {
        return;
    }
    if (state.drag->focus_surface == 0) {
        cancel_drag(state);
        return;
    }
    auto* target = find_client(state, static_cast<pwm::u32>(state.drag->focus_surface >> 32U));
    auto* source = find_client(state, state.drag->client);
    if (target != nullptr && !target->closed) {
        static_cast<void>(target->display.send_data_device_drop());
        static_cast<void>(target->connection.flush());
    }
    if (source != nullptr && !source->closed) {
        static_cast<void>(source->display.send_data_source_dnd_drop_performed(state.drag->source));
        static_cast<void>(source->connection.flush());
    }
    state.drag->dropped = true;
    state.drag->focus_surface = 0;
    state.drag->focus_offer = 0;
}

auto on_data_drag(void* context, const wayland::data_drag_event& event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto& state = *session.compositor;
    cancel_drag(state);
    const auto mime_types = session.display.data_source_mime_types(event.source);
    state.drag = drag_source{
        .client = session.index,
        .source = event.source,
        .actions = session.display.data_source_actions(event.source),
        .mime_types = std::vector<std::string>{mime_types.begin(), mime_types.end()},
    };
}

auto on_data_offer_actions(void* context, const wayland::data_offer_actions_event& event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto& state = *session.compositor;
    if (!state.drag || state.drag->source != event.source_object) {
        return;
    }
    state.drag->action = event.action;
    auto* source = find_client(state, event.source_client);
    if (source != nullptr && !source->closed && event.action != 0) {
        static_cast<void>(
            source->display.send_data_source_action(event.source_object, event.action));
        static_cast<void>(source->connection.flush());
    }
}

auto on_data_offer_finished(void* context, const wayland::data_offer_finished_event& event)
    -> void {
    auto& session = *static_cast<client_session*>(context);
    auto& state = *session.compositor;
    if (!state.drag || state.drag->source != event.source_object) {
        return;
    }
    auto* source = find_client(state, event.source_client);
    if (source != nullptr && !source->closed) {
        if (state.drag->action != 0) {
            static_cast<void>(
                source->display.send_data_source_action(event.source_object, state.drag->action));
        }
        static_cast<void>(source->display.send_data_source_dnd_finished(event.source_object));
        static_cast<void>(source->connection.flush());
    }
    end_drag(state);
}

auto on_data_offer_accept(void* context, const wayland::data_offer_accept_event& event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto* source = find_client(*session.compositor, event.source_client);
    if (source == nullptr || source->closed || event.mime_type.empty()) {
        return;
    }
    static_cast<void>(
        source->display.send_data_source_target(event.source_object, event.mime_type));
    static_cast<void>(source->connection.flush());
}

auto close_client(client_session& session) -> void {
    if (session.closed) {
        return;
    }
    if (session.compositor->clipboard && session.compositor->clipboard->client == session.index) {
        session.compositor->clipboard.reset();
        broadcast_selection(*session.compositor);
    }
    session.closed = true;
    static_cast<void>(session.compositor->loop->remove(session.connection.fd()));
    session.connection.disconnect();
}

auto on_client(void* context, event::poll_event) -> void {
    auto& session = *static_cast<client_session*>(context);
    if (auto received = session.connection.receive_available(); !received) {
        std::println(stderr, "client {} receive error: {}", session.index,
                     received.error().message);
        close_client(session);
        return;
    }
    if (auto dispatched = session.connection.dispatch_available(); !dispatched) {
        std::println(stderr, "client {} dispatch error: {}", session.index,
                     dispatched.error().message);
        close_client(session);
        return;
    }
    if (auto flushed = session.connection.flush(); !flushed) {
        std::println(stderr, "client {} flush error: {}", session.index, flushed.error().message);
        close_client(session);
        return;
    }
    if (session.connection.closed()) {
        close_client(session);
    }
}

auto accept_client(compositor_state& state) -> void {
    auto accepted = state.listener->accept();
    if (!accepted) {
        return;
    }
    auto session =
        std::make_unique<client_session>(state, state.next_client++, std::move(*accepted));
    if (auto started = session->display.start(); !started) {
        std::println(stderr, "display start failed: {}", started.error().message);
        return;
    }
    session->display.advertise_core(state.output->mode.width_px(), state.output->mode.height_px(),
                                    state.output->mode.refresh_hz());
    session->display.set_surface_commit_callback(on_client_commit, session.get());
    session->display.set_presentation_feedback_callback(on_presentation_feedback, session.get());
    session->display.set_data_source_callback(on_data_source, session.get());
    session->display.set_data_source_destroyed_callback(on_data_source_destroyed, session.get());
    session->display.set_data_offer_receive_callback(on_data_offer_receive, session.get());
    session->display.set_data_offer_accept_callback(on_data_offer_accept, session.get());
    session->display.set_data_drag_callback(on_data_drag, session.get());
    session->display.set_data_offer_actions_callback(on_data_offer_actions, session.get());
    session->display.set_data_offer_finished_callback(on_data_offer_finished, session.get());
    if (auto added = state.loop->add(session->connection.fd(), event::poll_event::readable,
                                     on_client, session.get());
        !added) {
        return;
    }
    std::println("client {} connected", session->index);
    state.clients.push_back(std::move(session));
}

auto on_listener(void* context, event::poll_event) -> void {
    accept_client(*static_cast<compositor_state*>(context));
}

auto find_client(compositor_state& state, pwm::u32 index) -> client_session* {
    for (const auto& session : state.clients) {
        if (session->index == index) {
            return session.get();
        }
    }
    return nullptr;
}

auto on_input_event(void* context, event::poll_event) -> void {
    auto& state = *static_cast<compositor_state*>(context);
    if (auto dispatched = state.input->dispatch(); !dispatched) {
        std::println(stderr, "input error: {}", dispatched.error().message);
        return;
    }
    while (auto input_event = state.input->next_event()) {
        if (input_event->type == input::input_event_type::keyboard_key) {
            for (const auto& session : state.clients) {
                if (session->closed) {
                    continue;
                }
                static_cast<void>(session->display.send_keyboard_key(
                    input_event->time_ms, input_event->key_code + 8, input_event->pressed));
                static_cast<void>(session->connection.flush());
            }
            continue;
        }
        if (input_event->type == input::input_event_type::pointer_motion) {
            state.cursor_x = std::clamp(state.cursor_x + input_event->dx, 0.0,
                                        static_cast<pwm::f64>(state.output->mode.width_px() - 1));
            state.cursor_y = std::clamp(state.cursor_y + input_event->dy, 0.0,
                                        static_cast<pwm::f64>(state.output->mode.height_px() - 1));
            update_drag_focus(state);
            motion_drag(state, input_event->time_ms);
            const auto hit = state.scene->hit_test(state.cursor_x, state.cursor_y);
            const pwm::u64 next_focus = hit.value_or(0);
            if (next_focus != state.pointer_focus) {
                if (state.pointer_focus != 0) {
                    auto* old_client =
                        find_client(state, static_cast<pwm::u32>(state.pointer_focus >> 32U));
                    if (old_client != nullptr) {
                        static_cast<void>(old_client->display.send_pointer_leave(
                            static_cast<wayland::object_id>(state.pointer_focus)));
                        static_cast<void>(old_client->connection.flush());
                    }
                }
                state.pointer_focus = next_focus;
                if (next_focus != 0) {
                    auto* client = find_client(state, static_cast<pwm::u32>(next_focus >> 32U));
                    const auto bounds = state.scene->surface_bounds(next_focus);
                    if (client != nullptr && bounds) {
                        static_cast<void>(client->display.send_pointer_enter(
                            static_cast<wayland::object_id>(next_focus), state.cursor_x - bounds->x,
                            state.cursor_y - bounds->y));
                        static_cast<void>(client->connection.flush());
                    }
                }
            }
            if (state.pointer_focus != 0) {
                auto* client =
                    find_client(state, static_cast<pwm::u32>(state.pointer_focus >> 32U));
                const auto bounds = state.scene->surface_bounds(state.pointer_focus);
                if (client != nullptr && bounds) {
                    static_cast<void>(client->display.send_pointer_motion(
                        input_event->time_ms, state.cursor_x - bounds->x,
                        state.cursor_y - bounds->y));
                    static_cast<void>(client->display.send_pointer_frame());
                    static_cast<void>(client->connection.flush());
                }
            }
        } else if (input_event->type == input::input_event_type::pointer_button &&
                   state.pointer_focus != 0) {
            if (state.drag && !input_event->pressed) {
                drop_drag(state);
            }
            auto* client = find_client(state, static_cast<pwm::u32>(state.pointer_focus >> 32U));
            if (client != nullptr) {
                static_cast<void>(client->display.send_pointer_button(
                    input_event->time_ms, input_event->button, input_event->pressed));
                static_cast<void>(client->display.send_pointer_frame());
                static_cast<void>(client->connection.flush());
            }
        } else if (input_event->type == input::input_event_type::pointer_axis &&
                   state.pointer_focus != 0) {
            auto* client = find_client(state, static_cast<pwm::u32>(state.pointer_focus >> 32U));
            if (client != nullptr) {
                static_cast<void>(client->display.send_pointer_axis(
                    input_event->time_ms, input_event->axis, input_event->axis_value));
                static_cast<void>(client->display.send_pointer_frame());
                static_cast<void>(client->connection.flush());
            }
        } else if (input_event->type == input::input_event_type::touch_down) {
            const auto hit = state.scene->hit_test(input_event->x, input_event->y);
            if (hit && input_event->touch_slot < state.touch_focus.size()) {
                state.touch_focus[input_event->touch_slot] = *hit;
                auto* client = find_client(state, static_cast<pwm::u32>(*hit >> 32U));
                const auto bounds = state.scene->surface_bounds(*hit);
                if (client != nullptr && bounds) {
                    static_cast<void>(client->display.send_touch_down(
                        static_cast<wayland::object_id>(*hit), input_event->time_ms,
                        static_cast<pwm::i32>(input_event->touch_slot), input_event->x - bounds->x,
                        input_event->y - bounds->y));
                    static_cast<void>(client->display.send_touch_frame());
                    static_cast<void>(client->connection.flush());
                }
            }
        } else if (input_event->type == input::input_event_type::touch_motion &&
                   input_event->touch_slot < state.touch_focus.size()) {
            const pwm::u64 focus = state.touch_focus[input_event->touch_slot];
            if (focus != 0) {
                auto* client = find_client(state, static_cast<pwm::u32>(focus >> 32U));
                const auto bounds = state.scene->surface_bounds(focus);
                if (client != nullptr && bounds) {
                    static_cast<void>(client->display.send_touch_motion(
                        input_event->time_ms, static_cast<pwm::i32>(input_event->touch_slot),
                        input_event->x - bounds->x, input_event->y - bounds->y));
                    static_cast<void>(client->display.send_touch_frame());
                    static_cast<void>(client->connection.flush());
                }
            }
        } else if (input_event->type == input::input_event_type::touch_up &&
                   input_event->touch_slot < state.touch_focus.size()) {
            const pwm::u64 focus = state.touch_focus[input_event->touch_slot];
            state.touch_focus[input_event->touch_slot] = 0;
            if (focus != 0) {
                auto* client = find_client(state, static_cast<pwm::u32>(focus >> 32U));
                if (client != nullptr) {
                    static_cast<void>(client->display.send_touch_up(
                        input_event->time_ms, static_cast<pwm::i32>(input_event->touch_slot)));
                    static_cast<void>(client->display.send_touch_frame());
                    static_cast<void>(client->connection.flush());
                }
            }
        }
    }
}

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
    add_output_properties(state, *(*state.framebuffers)[index], request.value());

    pwm::u32 flags = drm::atomic_page_flip_event | drm::atomic_nonblock;
    if (state.allow_modeset) {
        flags |= drm::atomic_allow_modeset;
    }
    auto committed = state.device->commit_atomic(request.value(), flags);
    if (!committed) {
        return std::unexpected(committed.error());
    }
    state.allow_modeset = false;
    state.waiting_for_flip = true;
    state.waiting_ticks = 0;
    return {};
}

auto render_next_frame(compositor_state& state) -> pwm::result<void> {
    const pwm::u32 index = state.next_buffer;
    const auto& output = *state.output;
    if (auto rendered = state.renderer->render(
            (*state.images)[index]->native_handle(), output.mode.width_px(),
            output.mode.height_px(), state.scene->scene_graph().root(), state.scene->textures(), {},
            {.red = 0.03F, .green = 0.04F, .blue = 0.06F, .alpha = 1.0F});
        !rendered) {
        return std::unexpected(rendered.error());
    }
    if (auto presented = present_frame(state, index); !presented) {
        return std::unexpected(presented.error());
    }
    state.next_buffer = (index + 1) % buffer_count;
    return {};
}

auto flush_presentation_feedback(compositor_state& state) -> void {
    if (state.pending_presentations.empty()) {
        return;
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    const auto nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() % 1000000000;
    for (const pending_feedback& pending : state.pending_presentations) {
        for (const auto& session : state.clients) {
            if (session->index == pending.client && !session->closed) {
                static_cast<void>(session->display.send_presentation_feedback(
                    pending.feedback, static_cast<pwm::u32>(seconds),
                    static_cast<pwm::u32>(nanoseconds), 16666667));
                static_cast<void>(session->connection.flush());
                break;
            }
        }
    }
    state.pending_presentations.clear();
}

auto on_page_flip(void* context) -> void {
    auto& state = *static_cast<compositor_state*>(context);
    state.waiting_for_flip = false;
    state.waiting_ticks = 0;
    flush_presentation_feedback(state);
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
    if (argc > 3) {
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
    if (state.listener != nullptr) {
        if (auto added =
                loop.add(state.listener->fd(), event::poll_event::readable, on_listener, &state);
            !added) {
            return std::unexpected(added.error());
        }
    }
    if (state.input != nullptr) {
        if (auto added =
                loop.add(state.input->fd(), event::poll_event::readable, on_input_event, &state);
            !added) {
            return std::unexpected(added.error());
        }
    }
    if (auto armed = state.timer->arm_ms(16); !armed) {
        return std::unexpected(armed.error());
    }
    state.loop = &loop;
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
    pwm::raii<render::scene_renderer> renderer;
};

auto create_vulkan_runtime(vulkan_runtime& runtime, pwm::u32 width_px, pwm::u32 height_px) -> void {
    const vk::application_info application{
        .name = "powerwm_wayland_kms_compositor",
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

    auto vertex = require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.vert.spv"));
    auto fragment = require_value(load_spirv(std::string{POWERWM_SHADER_DIR} + "/scene.frag.spv"));
    const render::target_format format =
        runtime.images[0]->pixel_format() == vk::dma_buf_pixel_format::rgba8888
            ? render::target_format::rgba8
            : render::target_format::bgra8;
    runtime.renderer = pwm::raii{require_value(render::scene_renderer::create(
        runtime.device.value(), format, std::span<const std::byte>{vertex},
        std::span<const std::byte>{fragment}))};
}

}

int main(int argc, char** argv) {
    const auto frame_count = parse_frame_count(argc, argv);
    if (!frame_count) {
        std::println(stderr, "usage: powerwm_wayland_kms_compositor [frame-count] [socket-path]");
        return EXIT_FAILURE;
    }

    const std::string socket_path = argc >= 3 ? argv[2] : "/tmp/powerwm-kms-wayland.sock";
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

    auto input_result = input::context::create("seat0");
    pwm::raii<input::context> input_context;
    if (input_result) {
        input_context = pwm::raii{std::move(*input_result)};
    } else {
        std::println(stderr, "warning: libinput unavailable");
    }

    wayland::surface_scene surface_scene{graphics.device.value(), output.mode.width_px(),
                                         output.mode.height_px()};
    auto listener = pwm::raii{require_value(transport::listener::create(socket_path))};
    auto timer = pwm::raii{require_value(event::timer_fd::create())};

    compositor_state state{};
    state.device = &device;
    state.session = session;
    state.output = &output;
    state.properties = &ids;
    state.mode_blob = &blob.value();
    state.framebuffers = &framebuffer_ptrs;
    state.renderer = &graphics.renderer.value();
    state.scene = &surface_scene;
    state.images = &image_ptrs;
    state.listener = &listener.value();
    state.input = input_context.is_owned() ? &input_context.value() : nullptr;
    state.timer = &timer.value();
    state.session_enabled = session == nullptr || session->is_enabled();
    state.frame_count = *frame_count;
    state.cursor_x = static_cast<pwm::f64>(output.mode.width_px()) / 2.0;
    state.cursor_y = static_cast<pwm::f64>(output.mode.height_px()) / 2.0;

    require_value(run_loop(state));
    std::println("presented {} frames", state.frame);

    if (session == nullptr && device.is_master()) {
        require_value(device.drop_master());
    }
    return EXIT_SUCCESS;
}
