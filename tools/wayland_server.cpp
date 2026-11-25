// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <base/result.hpp>
#include <platform/event/event_loop.hpp>
#include <platform/event/timer_fd.hpp>
#include <platform/wayland/socket.hpp>
#include <wayland/connection.hpp>
#include <wayland/display.hpp>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace event = pwm::platform::event;
namespace transport = pwm::platform::wayland;
namespace wayland = pwm::wayland;

using pwm::u32;

struct server_state;

struct client_session {
    client_session(server_state& server, u32 index, transport::stream stream) noexcept;

    server_state* server = nullptr;
    u32 index = 0;
    wayland::connection connection;
    wayland::display_service display;
    bool closed = false;
};

struct server_state {
    pwm::raii<transport::listener> listener;
    event::event_loop loop;
    pwm::raii<event::timer_fd> timer;
    std::vector<std::unique_ptr<client_session>> clients;
    u32 next_client = 1;
    u32 elapsed_seconds = 0;
    u32 duration_seconds = 30;
    bool running = true;
};

auto on_client_commit(void* context, const wayland::surface_commit_event& commit) -> void {
    auto& session = *static_cast<client_session*>(context);
    std::println("client {} surface {} buffer {} {}x{} stride {} format {}", session.index,
                 commit.surface, commit.buffer, commit.width, commit.height, commit.stride,
                 commit.format);
}

client_session::client_session(server_state& server, u32 index, transport::stream stream) noexcept
    : server(&server), index(index), connection(std::move(stream)), display(connection) {
}

auto close_client(client_session& session) -> void {
    if (session.closed) {
        return;
    }
    session.closed = true;
    static_cast<void>(session.server->loop.remove(session.connection.fd()));
    session.connection.disconnect();
}

auto on_client(void* context, event::poll_event) -> void {
    auto& session = *static_cast<client_session*>(context);
    auto received = session.connection.receive_available();
    if (!received) {
        std::println(stderr, "client {} receive error: {}", session.index,
                     received.error().message);
        close_client(session);
        return;
    }
    auto dispatched = session.connection.dispatch_available();
    if (!dispatched) {
        std::println(stderr, "client {} dispatch error: {}", session.index,
                     dispatched.error().message);
        close_client(session);
        return;
    }
    session.display.process_discards();
    auto flushed = session.connection.flush();
    if (!flushed) {
        std::println(stderr, "client {} flush error: {}", session.index, flushed.error().message);
        close_client(session);
        return;
    }
    if (session.connection.closed()) {
        close_client(session);
    }
}

auto accept_client(server_state& server) -> void {
    auto accepted = server.listener->accept();
    if (!accepted) {
        if (accepted.error().code != pwm::error_code::cancelled) {
            std::println(stderr, "accept error: {}", accepted.error().message);
        }
        return;
    }
    auto session =
        std::make_unique<client_session>(server, server.next_client++, std::move(*accepted));
    if (auto started = session->display.start(); !started) {
        std::println(stderr, "display start failed: {}", started.error().message);
        return;
    }
    session->display.advertise_core(1280, 720, 60);
    session->display.set_surface_commit_callback(on_client_commit, session.get());
    std::println("client {} connected", session->index);
    const pwm::handle_t fd = session->connection.fd();
    if (auto added = server.loop.add(fd, event::poll_event::readable, on_client, session.get());
        !added) {
        std::println(stderr, "client fd registration failed: {}", added.error().message);
        return;
    }
    server.clients.push_back(std::move(session));
}

auto on_listener(void* context, event::poll_event) -> void {
    auto& server = *static_cast<server_state*>(context);
    accept_client(server);
}

auto on_timer(void* context, event::poll_event) -> void {
    auto& server = *static_cast<server_state*>(context);
    auto expirations = server.timer->read_expirations();
    if (!expirations) {
        server.running = false;
        return;
    }
    server.elapsed_seconds += static_cast<pwm::u32>(*expirations);
    if (server.elapsed_seconds >= server.duration_seconds) {
        server.running = false;
    }
}

auto parse_u32(std::string_view text) -> pwm::result<u32> {
    pwm::u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::unexpected(
            pwm::make_error(pwm::error_code::invalid_argument, "invalid numeric argument"));
    }
    return value;
}

auto default_socket_path() -> std::string {
    if (const char* runtime = std::getenv("XDG_RUNTIME_DIR"); runtime != nullptr) {
        return std::string{runtime} + "/powerwm-wayland-0";
    }
    return "/tmp/powerwm-wayland-0";
}

auto run_server(std::string_view socket_path, u32 duration_seconds) -> pwm::result<void> {
    server_state server;
    server.duration_seconds = duration_seconds;
    auto listener = transport::listener::create(socket_path);
    if (!listener) {
        return std::unexpected(listener.error());
    }
    server.listener = pwm::raii{std::move(*listener)};
    auto timer = event::timer_fd::create();
    if (!timer) {
        return std::unexpected(timer.error());
    }
    server.timer = pwm::raii{std::move(*timer)};

    if (auto added = server.loop.add(server.listener->fd(), event::poll_event::readable,
                                     on_listener, &server);
        !added) {
        return std::unexpected(added.error());
    }
    if (auto added =
            server.loop.add(server.timer->fd(), event::poll_event::readable, on_timer, &server);
        !added) {
        return std::unexpected(added.error());
    }
    if (auto armed = server.timer->arm_ms(1000); !armed) {
        return std::unexpected(armed.error());
    }

    std::println("powerwm wayland server listening on {}", socket_path);
    while (server.running) {
        auto dispatched = server.loop.run_once(100);
        if (!dispatched) {
            return std::unexpected(dispatched.error());
        }
    }
    return {};
}

}

int main(int argc, char** argv) {
    std::string socket_path = default_socket_path();
    pwm::u32 duration_seconds = 30;
    if (argc >= 2) {
        socket_path = argv[1];
    }
    if (argc >= 3) {
        auto parsed = parse_u32(argv[2]);
        if (!parsed) {
            std::println(stderr, "invalid duration");
            return EXIT_FAILURE;
        }
        duration_seconds = *parsed;
    }
    auto result = run_server(socket_path, duration_seconds);
    if (!result) {
        std::println(stderr, "server error: {}", result.error().message);
        return EXIT_FAILURE;
    }
    std::println("powerwm wayland server stopped");
    return EXIT_SUCCESS;
}
