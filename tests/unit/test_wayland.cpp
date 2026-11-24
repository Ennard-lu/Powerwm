// SPDX-License-Identifier: Apache-2.0

#include <base/result.hpp>
#include <base/types.hpp>
#include <platform/wayland/mapped_region.hpp>
#include <platform/wayland/socket.hpp>
#include <wayland/connection.hpp>
#include <wayland/dispatch.hpp>
#include <wayland/display.hpp>
#include <wayland/object_table.hpp>
#include <wayland/protocol.hpp>
#include <wayland/wire.hpp>
#include <wayland/xml.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>
#include <wayland_bindings.hpp>
#include <xdg_shell_bindings.hpp>

namespace {

namespace wayland = pwm::wayland;
namespace generated = pwm::wayland::generated;

using pwm::i32;
using pwm::u16;
using pwm::u32;
using wayland::object_id;

struct display_implementation {
    u32 sync_callback = 0;
    u32 registry_id = 0;

    auto request_sync(u32 callback) -> pwm::result<void> {
        sync_callback = callback;
        return {};
    }

    auto request_get_registry(u32 registry) -> pwm::result<void> {
        registry_id = registry;
        return {};
    }
};

struct partial_display_implementation {
    auto request_sync(u32) -> void {
    }
};

struct wm_base_implementation {
    object_id surface_id = 0;
    object_id xdg_surface_id = 0;

    auto request_get_xdg_surface(object_id id, object_id surface) -> pwm::result<void> {
        xdg_surface_id = id;
        surface_id = surface;
        return {};
    }
};

auto test_xml_parser() -> bool {
    constexpr std::string_view input = R"(<?xml version="1.0"?>
<protocol name="sample">
  <interface name="sample_object" version="3">
    <request name="do_thing" since="2">
      <arg name="value" type="uint"/>
      <arg name="name" type="string"/>
    </request>
    <event name="done">
      <arg name="result" type="int"/>
    </event>
    <enum name="error">
      <entry name="bad" value="1"/>
    </enum>
  </interface>
</protocol>)";

    auto tree = wayland::parse_xml(input);
    if (!tree) {
        return false;
    }
    auto protocol = wayland::build_protocol(*tree);
    if (!protocol || protocol->interfaces.size() != 1) {
        return false;
    }
    const auto& interface = protocol->interfaces.front();
    return interface.name == "sample_object" && interface.version == 3 &&
           interface.requests.size() == 1 && interface.requests.front().name == "do_thing" &&
           interface.requests.front().since == 2 &&
           interface.requests.front().arguments.size() == 2 && interface.events.size() == 1 &&
           interface.events.front().name == "done" && interface.enums.size() == 1 &&
           interface.enums.front().entries.front().value == 1;
}

auto test_wire_roundtrip() -> bool {
    wayland::protocol_writer writer;
    if (!writer.begin_message(7, 3)) {
        return false;
    }
    if (!writer.write_i32(-42) || !writer.write_u32(9) || !writer.write_fixed(1.5) ||
        !writer.write_string("powerwm") || !writer.write_new_id(11) || !writer.write_fd(23)) {
        return false;
    }
    const std::array<std::byte, 4> array{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    if (!writer.write_array(array)) {
        return false;
    }
    if (!writer.end_message()) {
        return false;
    }

    auto header = wayland::decode_header(writer.bytes());
    if (!header || header->object != 7 || header->opcode != 3) {
        return false;
    }

    const std::array<pwm::handle_t, 1> fds{23};
    wayland::protocol_reader reader{writer.bytes().subspan(sizeof(wayland::message_header)), fds};
    auto integer = reader.read_i32();
    auto unsigned_value = reader.read_u32();
    auto fixed = reader.read_fixed();
    auto text = reader.read_string();
    auto new_id = reader.read_new_id();
    auto fd = reader.read_fd();
    auto bytes = reader.read_array();
    if (!integer || *integer != -42 || !unsigned_value || *unsigned_value != 9 || !fixed ||
        *fixed != 1.5 || !text || *text != "powerwm" || !new_id || *new_id != 11 || !fd ||
        *fd != 23 || !bytes || bytes->size() != 4) {
        return false;
    }
    return reader.remaining() == 0;
}

auto make_request(object_id object, u16 opcode, u32 argument) -> wayland::protocol_writer {
    wayland::protocol_writer writer;
    static_cast<void>(writer.begin_message(object, opcode));
    static_cast<void>(writer.write_new_id(argument));
    static_cast<void>(writer.end_message());
    return writer;
}

auto test_reflection_dispatch() -> bool {
    auto writer = make_request(1, 0, 42);
    wayland::protocol_reader sync_reader{writer.bytes().subspan(sizeof(wayland::message_header)),
                                         std::span<const pwm::handle_t>{}};
    display_implementation implementation;
    auto dispatched = wayland::dispatch_request<display_implementation, generated::wl_display>(
        implementation, 0, sync_reader);
    if (!dispatched || implementation.sync_callback != 42) {
        return false;
    }

    auto registry_writer = make_request(1, 1, 77);
    wayland::protocol_reader registry_reader{
        registry_writer.bytes().subspan(sizeof(wayland::message_header)),
        std::span<const pwm::handle_t>{}};
    auto registry_dispatched =
        wayland::dispatch_request<display_implementation, generated::wl_display>(implementation, 1,
                                                                                 registry_reader);
    if (!registry_dispatched || implementation.registry_id != 77) {
        return false;
    }

    auto partial_writer = make_request(1, 1, 88);
    wayland::protocol_reader partial_reader{
        partial_writer.bytes().subspan(sizeof(wayland::message_header)),
        std::span<const pwm::handle_t>{}};
    partial_display_implementation partial;
    auto missing = wayland::dispatch_request<partial_display_implementation, generated::wl_display>(
        partial, 1, partial_reader);
    return !missing && missing.error().code == pwm::error_code::unsupported;
}

auto test_xdg_shell_dispatch() -> bool {
    wayland::protocol_writer writer;
    static_cast<void>(writer.begin_message(10, 2));
    static_cast<void>(writer.write_new_id(51));
    static_cast<void>(writer.write_object(52));
    static_cast<void>(writer.end_message());

    wayland::protocol_reader reader{writer.bytes().subspan(sizeof(wayland::message_header)),
                                    std::span<const pwm::handle_t>{}};
    wm_base_implementation implementation;
    auto dispatched = wayland::dispatch_request<wm_base_implementation, generated::xdg_wm_base>(
        implementation, 2, reader);
    return dispatched && implementation.xdg_surface_id == 51 && implementation.surface_id == 52;
}

auto test_object_table() -> bool {
    wayland::object_table table;
    display_implementation implementation;
    if (!wayland::add_object<display_implementation, generated::wl_display>(table, 1,
                                                                            implementation)) {
        return false;
    }
    if (table.size() != 1 || table.find(1) == nullptr || table.find(2) != nullptr) {
        return false;
    }

    auto writer = make_request(1, 0, 64);
    wayland::protocol_reader reader{writer.bytes().subspan(sizeof(wayland::message_header)),
                                    std::span<const pwm::handle_t>{}};
    auto dispatched = table.dispatch(1, 0, reader);
    if (!dispatched || implementation.sync_callback != 64) {
        return false;
    }

    wayland::protocol_reader missing_reader{std::span<const std::byte>{},
                                            std::span<const pwm::handle_t>{}};
    if (table.dispatch(2, 0, missing_reader)) {
        return false;
    }
    return table.remove(1) && table.size() == 0;
}

auto test_socket_transport() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-transport.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto client_result = transport::connect_stream(path);
    if (!client_result) {
        return false;
    }
    auto client = std::move(*client_result);
    auto server_result = listener.accept();
    if (!server_result) {
        return false;
    }

    wayland::connection server{std::move(*server_result)};
    wayland::display_service display{server};
    if (!display.start()) {
        return false;
    }
    display.add_global("wl_demo", 1);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(1, 0));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(1, 0));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    const auto encoded = requests.bytes();
    auto partial = client.send(encoded.first(sizeof(wayland::message_header)), {});
    if (!partial || partial->bytes != sizeof(wayland::message_header)) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto incomplete = server.dispatch_available();
    if (!incomplete || *incomplete != 0) {
        return false;
    }

    auto sent = client.send(encoded.subspan(sizeof(wayland::message_header)), {});
    if (!sent || sent->bytes != encoded.size() - sizeof(wayland::message_header)) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 3) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::array<std::byte, 4096> received{};
    std::vector<std::byte> input;
    for (pwm::i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(received, std::span<pwm::handle_t>{});
        if (!result) {
            return false;
        }
        input.insert(input.end(), received.begin(), received.begin() + result->bytes);
        if (result->would_block || result->closed || !input.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    if (input.empty()) {
        return false;
    }

    pwm::usize offset = 0;
    pwm::usize messages = 0;
    bool saw_global = false;
    bool saw_callback = false;
    bool saw_delete = false;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(std::span<const std::byte>{input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 2 && header->opcode == 0) {
            auto event = generated::wl_registry::event_global::decode(reader);
            if (!event || std::get<0>(event->arguments) != 1 ||
                std::get<1>(event->arguments) != "wl_demo" || std::get<2>(event->arguments) != 1) {
                return false;
            }
            saw_global = true;
        } else if ((header->object == 3 || header->object == 4) && header->opcode == 0) {
            auto event = generated::wl_callback::event_done::decode(reader);
            if (!event) {
                return false;
            }
            saw_callback = true;
        } else if (header->object == 1 && header->opcode == 1) {
            auto event = generated::wl_display::event_delete_id::decode(reader);
            if (!event) {
                return false;
            }
            saw_delete = true;
        } else {
            return false;
        }
        offset += header->size;
        ++messages;
    }
    return offset == input.size() && messages == 5 && saw_global && saw_callback && saw_delete;
}

auto test_core_objects() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-core.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto client_result = transport::connect_stream(path);
    if (!client_result) {
        return false;
    }
    auto client = std::move(*client_result);
    auto server_result = listener.accept();
    if (!server_result) {
        return false;
    }
    wayland::connection server{std::move(*server_result)};
    wayland::display_service display{server};
    if (!display.start()) {
        return false;
    }
    const u32 compositor_global = display.add_global("wl_compositor", 4);
    const u32 shm_global = display.add_global("wl_shm", 1);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(compositor_global));
    static_cast<void>(requests.write_string("wl_compositor"));
    static_cast<void>(requests.write_u32(4));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(shm_global));
    static_cast<void>(requests.write_string("wl_shm"));
    static_cast<void>(requests.write_u32(1));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 0));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 1));
    static_cast<void>(requests.write_new_id(8));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(8, 1));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(100));
    static_cast<void>(requests.write_i32(100));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(7, 1));
    static_cast<void>(requests.write_object(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(7, 2));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(100));
    static_cast<void>(requests.write_i32(100));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(7, 3));
    static_cast<void>(requests.write_new_id(9));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(7, 6));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(7, 0));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        std::println(stderr, "core receive failed");
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched) {
        std::println(stderr, "core dispatch failed: {}", dispatched.error().message);
        return false;
    }
    if (*dispatched != 11) {
        std::println(stderr, "core dispatch count {}", *dispatched);
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::array<std::byte, 4096> received{};
    std::vector<std::byte> input;
    for (pwm::i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(received, std::span<pwm::handle_t>{});
        if (!result) {
            return false;
        }
        input.insert(input.end(), received.begin(), received.begin() + result->bytes);
        if (result->would_block || result->closed || !input.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    pwm::usize offset = 0;
    pwm::u32 globals = 0;
    pwm::u32 formats = 0;
    bool saw_callback = false;
    bool saw_callback_delete = false;
    bool saw_surface_delete = false;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(std::span<const std::byte>{input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 2 && header->opcode == 0) {
            auto event = generated::wl_registry::event_global::decode(reader);
            if (!event) {
                return false;
            }
            ++globals;
        } else if (header->object == 6 && header->opcode == 0) {
            auto event = generated::wl_shm::event_format::decode(reader);
            if (!event) {
                return false;
            }
            ++formats;
        } else if (header->object == 9 && header->opcode == 0) {
            auto event = generated::wl_callback::event_done::decode(reader);
            if (!event) {
                return false;
            }
            saw_callback = true;
        } else if (header->object == 1 && header->opcode == 1) {
            auto event = generated::wl_display::event_delete_id::decode(reader);
            if (!event) {
                return false;
            }
            if (std::get<0>(event->arguments) == 9) {
                saw_callback_delete = true;
            } else if (std::get<0>(event->arguments) == 7) {
                saw_surface_delete = true;
            }
        } else {
            return false;
        }
        offset += header->size;
    }
    return offset == input.size() && globals == 2 && formats == 2 && saw_callback &&
           saw_callback_delete && saw_surface_delete && server.objects().find(7) == nullptr;
}

auto test_xdg_shell_transport() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-xdg.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto client_result = transport::connect_stream(path);
    if (!client_result) {
        return false;
    }
    auto client = std::move(*client_result);
    auto server_result = listener.accept();
    if (!server_result) {
        return false;
    }
    wayland::connection server{std::move(*server_result)};
    wayland::display_service display{server};
    if (!display.start()) {
        return false;
    }
    const u32 compositor_global = display.add_global("wl_compositor", 4);
    const u32 xdg_global = display.add_global("xdg_wm_base", 1);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(compositor_global));
    static_cast<void>(requests.write_string("wl_compositor"));
    static_cast<void>(requests.write_u32(4));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(xdg_global));
    static_cast<void>(requests.write_string("xdg_wm_base"));
    static_cast<void>(requests.write_u32(1));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 2));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.write_object(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 1));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 1));
    static_cast<void>(requests.write_new_id(8));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 7) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::array<std::byte, 4096> received{};
    std::vector<std::byte> input;
    for (pwm::i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(received, std::span<pwm::handle_t>{});
        if (!result) {
            return false;
        }
        input.insert(input.end(), received.begin(), received.begin() + result->bytes);
        if (result->would_block || result->closed || !input.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    u32 configure_serial = 0;
    bool saw_toplevel_configure = false;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(std::span<const std::byte>{input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 8 && header->opcode == 0) {
            auto event = generated::xdg_toplevel::event_configure::decode(reader);
            if (!event) {
                return false;
            }
            saw_toplevel_configure = true;
        } else if (header->object == 6 && header->opcode == 0) {
            auto event = generated::xdg_surface::event_configure::decode(reader);
            if (!event) {
                return false;
            }
            configure_serial = std::get<0>(event->arguments);
        } else if (header->object == 2 && header->opcode == 0) {
            if (!generated::wl_registry::event_global::decode(reader)) {
                return false;
            }
        } else {
            return false;
        }
        offset += header->size;
    }
    if (!saw_toplevel_configure || configure_serial == 0) {
        return false;
    }

    wayland::protocol_writer second;
    static_cast<void>(second.begin_message(6, 4));
    static_cast<void>(second.write_u32(configure_serial));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(8, 2));
    static_cast<void>(second.write_string("powerwm"));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(8, 3));
    static_cast<void>(second.write_string("powerwm.test"));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(8, 0));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(6, 0));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(7, 0));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(4, 0));
    static_cast<void>(second.end_message());
    static_cast<void>(second.begin_message(5, 0));
    static_cast<void>(second.end_message());

    auto second_sent = client.send(second.bytes(), {});
    if (!second_sent || second_sent->bytes != second.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto second_dispatched = server.dispatch_available();
    if (!second_dispatched || *second_dispatched != 8) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::vector<std::byte> deletes;
    for (pwm::i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(received, std::span<pwm::handle_t>{});
        if (!result) {
            return false;
        }
        deletes.insert(deletes.end(), received.begin(), received.begin() + result->bytes);
        if (result->would_block || result->closed || !deletes.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    pwm::u32 delete_count = 0;
    offset = 0;
    while (offset + sizeof(wayland::message_header) <= deletes.size()) {
        auto header = wayland::decode_header(std::span<const std::byte>{deletes}.subspan(offset));
        if (!header || header->object != 1 || header->opcode != 1) {
            return false;
        }
        const auto payload = std::span<const std::byte>{deletes}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (!generated::wl_display::event_delete_id::decode(reader)) {
            return false;
        }
        offset += header->size;
        ++delete_count;
    }
    return delete_count == 5 && server.objects().find(8) == nullptr;
}

struct commit_capture {
    object_id surface = 0;
    object_id buffer = 0;
    u32 width = 0;
    u32 height = 0;
    u32 stride = 0;
    u32 format = 0;
    std::vector<std::byte> pixels;
};

auto capture_commit(void* context, const wayland::surface_commit_event& event) -> void {
    auto& capture = *static_cast<commit_capture*>(context);
    capture.surface = event.surface;
    capture.buffer = event.buffer;
    capture.width = static_cast<u32>(event.width);
    capture.height = static_cast<u32>(event.height);
    capture.stride = static_cast<u32>(event.stride);
    capture.format = event.format;
    capture.pixels.assign(event.pixels.begin(), event.pixels.end());
}

auto test_shm_commit() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-shm.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto client_result = transport::connect_stream(path);
    if (!client_result) {
        return false;
    }
    auto client = std::move(*client_result);
    auto server_result = listener.accept();
    if (!server_result) {
        return false;
    }
    wayland::connection server{std::move(*server_result)};
    wayland::display_service display{server};
    if (!display.start()) {
        return false;
    }
    commit_capture capture;
    display.set_surface_commit_callback(capture_commit, &capture);
    const u32 compositor_global = display.add_global("wl_compositor", 4);
    const u32 shm_global = display.add_global("wl_shm", 1);

    constexpr pwm::u32 width = 8;
    constexpr pwm::u32 height = 8;
    constexpr pwm::u32 stride = width * 4;
    constexpr pwm::usize pool_size = stride * height;
    auto file = transport::create_anonymous_file("powerwm-test-shm", pool_size);
    if (!file) {
        return false;
    }
    auto mapping = transport::mapped_region::create(file->fd(), 0, pool_size, true);
    if (!mapping) {
        return false;
    }
    auto pixels = mapping->writable_data();
    for (pwm::usize index = 0; index < pixels.size(); ++index) {
        pixels[index] = static_cast<std::byte>(index & 0xFFU);
    }

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(compositor_global));
    static_cast<void>(requests.write_string("wl_compositor"));
    static_cast<void>(requests.write_u32(4));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(shm_global));
    static_cast<void>(requests.write_string("wl_shm"));
    static_cast<void>(requests.write_u32(1));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 0));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.write_fd(file->fd()));
    static_cast<void>(requests.write_i32(static_cast<i32>(pool_size)));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 0));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(static_cast<i32>(width)));
    static_cast<void>(requests.write_i32(static_cast<i32>(height)));
    static_cast<void>(requests.write_i32(static_cast<i32>(stride)));
    static_cast<void>(requests.write_u32(0));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 1));
    static_cast<void>(requests.write_object(7));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 3));
    static_cast<void>(requests.write_new_id(8));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 6));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 9) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    return capture.surface == 5 && capture.buffer == 7 && capture.width == width &&
           capture.height == height && capture.stride == stride && capture.format == 0 &&
           capture.pixels.size() == pool_size && capture.pixels[0] == std::byte{0} &&
           capture.pixels[1] == std::byte{1} && capture.pixels[255] == std::byte{255};
}

auto test_output_and_seat() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-seat.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto client_result = transport::connect_stream(path);
    if (!client_result) {
        return false;
    }
    auto client = std::move(*client_result);
    auto server_result = listener.accept();
    if (!server_result) {
        return false;
    }
    wayland::connection server{std::move(*server_result)};
    wayland::display_service display{server};
    if (!display.start()) {
        return false;
    }
    const u32 output_global = display.add_global("wl_output", 4);
    const u32 seat_global = display.add_global("wl_seat", 2);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(output_global));
    static_cast<void>(requests.write_string("wl_output"));
    static_cast<void>(requests.write_u32(4));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(seat_global));
    static_cast<void>(requests.write_string("wl_seat"));
    static_cast<void>(requests.write_u32(2));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 0));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 1));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 2));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 6) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::array<std::byte, 4096> received{};
    std::array<pwm::handle_t, 4> received_fds{};
    pwm::u32 received_fd_count = 0;
    std::vector<std::byte> input;
    for (pwm::i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(received, received_fds);
        if (!result) {
            return false;
        }
        received_fd_count = static_cast<pwm::u32>(result->fds);
        input.insert(input.end(), received.begin(), received.begin() + result->bytes);
        if (result->would_block || result->closed || !input.empty()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    pwm::u32 output_events = 0;
    pwm::u32 seat_events = 0;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(std::span<const std::byte>{input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{
            payload, std::span<const pwm::handle_t>{received_fds.data(), received_fd_count}};
        if (header->object == 3) {
            switch (header->opcode) {
            case 0:
                if (!generated::wl_output::event_geometry::decode(reader)) {
                    return false;
                }
                break;
            case 1:
                if (!generated::wl_output::event_mode::decode(reader)) {
                    return false;
                }
                break;
            case 2:
                if (!generated::wl_output::event_done::decode(reader)) {
                    return false;
                }
                break;
            case 3:
                if (!generated::wl_output::event_scale::decode(reader)) {
                    return false;
                }
                break;
            case 4:
                if (!generated::wl_output::event_name::decode(reader)) {
                    return false;
                }
                break;
            case 5:
                if (!generated::wl_output::event_description::decode(reader)) {
                    return false;
                }
                break;
            default:
                return false;
            }
            ++output_events;
        } else if (header->object == 4) {
            if (header->opcode == 0) {
                if (!generated::wl_seat::event_capabilities::decode(reader)) {
                    return false;
                }
            } else if (header->opcode == 1) {
                if (!generated::wl_seat::event_name::decode(reader)) {
                    return false;
                }
            } else {
                return false;
            }
            ++seat_events;
        } else if (header->object == 6 && header->opcode == 0) {
            auto event = generated::wl_keyboard::event_keymap::decode(reader);
            if (!event) {
                return false;
            }
            auto owned = transport::file_descriptor::adopt(std::get<1>(event->arguments));
            static_cast<void>(owned);
        } else if (header->object == 2 && header->opcode == 0) {
            if (!generated::wl_registry::event_global::decode(reader)) {
                return false;
            }
        } else {
            return false;
        }
        offset += header->size;
    }
    if (output_events != 6 || seat_events != 2 || server.objects().find(5) == nullptr ||
        server.objects().find(6) == nullptr || server.objects().find(7) == nullptr) {
        std::println(stderr, "seat phase2 output={} seat={} p={} k={} t={}", output_events,
                     seat_events, server.objects().find(5) != nullptr,
                     server.objects().find(6) != nullptr, server.objects().find(7) != nullptr);
        return false;
    }

    if (!display.send_keyboard_enter(5) || !display.send_keyboard_modifiers(0, 0, 0, 0) ||
        !display.send_keyboard_key(1, 30, true) || !server.flush()) {
        return false;
    }
    std::array<std::byte, 1024> keyboard_bytes{};
    auto keyboard_result = client.receive(keyboard_bytes, std::span<pwm::handle_t>{});
    if (!keyboard_result) {
        return false;
    }
    const std::span<const std::byte> keyboard_input{keyboard_bytes.data(), keyboard_result->bytes};
    pwm::usize keyboard_offset = 0;
    pwm::u32 keyboard_events = 0;
    while (keyboard_offset + sizeof(wayland::message_header) <= keyboard_input.size()) {
        auto header = wayland::decode_header(keyboard_input.subspan(keyboard_offset));
        if (!header || header->object != 6) {
            return false;
        }
        const auto payload =
            keyboard_input.subspan(keyboard_offset + sizeof(wayland::message_header),
                                   header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->opcode == 1) {
            if (!generated::wl_keyboard::event_enter::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 3) {
            if (!generated::wl_keyboard::event_key::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 4) {
            if (!generated::wl_keyboard::event_modifiers::decode(reader)) {
                return false;
            }
        } else {
            return false;
        }
        keyboard_offset += header->size;
        ++keyboard_events;
    }
    if (keyboard_events != 3) {
        return false;
    }

    if (!display.send_pointer_enter(5, 1.0, 2.0) || !display.send_pointer_motion(2, 3.0, 4.0) ||
        !display.send_pointer_button(3, 0x110, true) || !display.send_pointer_axis(4, 0, 120.0) ||
        !display.send_pointer_frame() || !server.flush()) {
        return false;
    }
    std::array<std::byte, 1024> pointer_bytes{};
    auto pointer_result = client.receive(pointer_bytes, std::span<pwm::handle_t>{});
    if (!pointer_result) {
        return false;
    }
    const std::span<const std::byte> pointer_input{pointer_bytes.data(), pointer_result->bytes};
    pwm::usize pointer_offset = 0;
    pwm::u32 pointer_events = 0;
    while (pointer_offset + sizeof(wayland::message_header) <= pointer_input.size()) {
        auto header = wayland::decode_header(pointer_input.subspan(pointer_offset));
        if (!header || header->object != 5) {
            return false;
        }
        const auto payload = pointer_input.subspan(pointer_offset + sizeof(wayland::message_header),
                                                   header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->opcode == 0) {
            if (!generated::wl_pointer::event_enter::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 2) {
            if (!generated::wl_pointer::event_motion::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 3) {
            if (!generated::wl_pointer::event_button::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 4) {
            if (!generated::wl_pointer::event_axis::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 5) {
            if (!generated::wl_pointer::event_frame::decode(reader)) {
                return false;
            }
        } else {
            return false;
        }
        pointer_offset += header->size;
        ++pointer_events;
    }
    if (pointer_events != 5) {
        return false;
    }
    if (!display.send_touch_down(7, 5, 0, 1.0, 2.0) || !display.send_touch_motion(6, 0, 3.0, 4.0) ||
        !display.send_touch_up(7, 0) || !display.send_touch_frame() || !server.flush()) {
        return false;
    }
    std::array<std::byte, 1024> touch_bytes{};
    auto touch_result = client.receive(touch_bytes, std::span<pwm::handle_t>{});
    if (!touch_result) {
        return false;
    }
    const std::span<const std::byte> touch_input{touch_bytes.data(), touch_result->bytes};
    pwm::usize touch_offset = 0;
    pwm::u32 touch_events = 0;
    while (touch_offset + sizeof(wayland::message_header) <= touch_input.size()) {
        auto header = wayland::decode_header(touch_input.subspan(touch_offset));
        if (!header || header->object != 7) {
            return false;
        }
        const auto payload = touch_input.subspan(touch_offset + sizeof(wayland::message_header),
                                                 header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->opcode == 0) {
            if (!generated::wl_touch::event_down::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 1) {
            if (!generated::wl_touch::event_up::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 2) {
            if (!generated::wl_touch::event_motion::decode(reader)) {
                return false;
            }
        } else if (header->opcode == 3) {
            if (!generated::wl_touch::event_frame::decode(reader)) {
                return false;
            }
        } else {
            return false;
        }
        touch_offset += header->size;
        ++touch_events;
    }
    return touch_events == 4;
}

auto test_generated_descriptors() -> bool {
    if (generated::wl_display::name != "wl_display" || generated::wl_display::request_count != 2 ||
        generated::wl_display::event_count != 2) {
        return false;
    }
    if (generated::wl_display::request_names[0] != "sync" ||
        generated::wl_display::request_handler_names[0] != "request_sync" ||
        generated::xdg_wm_base::name != "xdg_wm_base" ||
        generated::xdg_wm_base::request_names[2] != "get_xdg_surface") {
        return false;
    }

    generated::wl_display::event_delete_id event{};
    std::get<0>(event.arguments) = 55;
    wayland::protocol_writer writer;
    auto encoded = generated::wl_display::event_delete_id::encode(writer, 1, event);
    if (!encoded) {
        return false;
    }
    wayland::protocol_reader reader{writer.bytes().subspan(sizeof(wayland::message_header)),
                                    std::span<const pwm::handle_t>{}};
    auto decoded = generated::wl_display::event_delete_id::decode(reader);
    return decoded && std::get<0>(decoded->arguments) == 55;
}

}

int main() {
    if (!test_xml_parser()) {
        std::println(stderr, "xml parser test failed");
        return EXIT_FAILURE;
    }
    if (!test_wire_roundtrip()) {
        std::println(stderr, "wire roundtrip test failed");
        return EXIT_FAILURE;
    }
    if (!test_reflection_dispatch()) {
        std::println(stderr, "reflection dispatch test failed");
        return EXIT_FAILURE;
    }
    if (!test_object_table()) {
        std::println(stderr, "object table test failed");
        return EXIT_FAILURE;
    }
    if (!test_xdg_shell_dispatch()) {
        std::println(stderr, "xdg-shell dispatch test failed");
        return EXIT_FAILURE;
    }
    if (!test_core_objects()) {
        std::println(stderr, "core object test failed");
        return EXIT_FAILURE;
    }
    if (!test_output_and_seat()) {
        std::println(stderr, "output/seat test failed");
        return EXIT_FAILURE;
    }
    if (!test_shm_commit()) {
        std::println(stderr, "shm commit test failed");
        return EXIT_FAILURE;
    }
    if (!test_xdg_shell_transport()) {
        std::println(stderr, "xdg-shell transport test failed");
        return EXIT_FAILURE;
    }
    if (!test_socket_transport()) {
        std::println(stderr, "socket transport test failed");
        return EXIT_FAILURE;
    }
    if (!test_generated_descriptors()) {
        std::println(stderr, "generated descriptor test failed");
        return EXIT_FAILURE;
    }
    std::println("wayland protocol tests passed");
    return EXIT_SUCCESS;
}
