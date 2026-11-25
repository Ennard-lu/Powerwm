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
#include <fractional_scale_bindings.hpp>
#include <layer_shell_bindings.hpp>
#include <linux_dmabuf_bindings.hpp>
#include <presentation_time_bindings.hpp>
#include <primary_selection_bindings.hpp>
#include <print>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>
#include <viewporter_bindings.hpp>
#include <wayland_bindings.hpp>
#include <xdg_decoration_bindings.hpp>
#include <xdg_output_bindings.hpp>
#include <xdg_shell_bindings.hpp>

namespace {

namespace wayland = pwm::wayland;
namespace generated = pwm::wayland::generated;
namespace transport = pwm::platform::wayland;

using pwm::i32;
using pwm::u16;
using pwm::u32;
using wayland::object_id;

auto drain_client(transport::stream& client, std::vector<std::byte>& input,
                  std::vector<pwm::handle_t>& fds) -> bool;

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
        std::println(stderr, "xdg send failed");
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
    const u32 output_global = display.add_global("wl_output", 4);

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
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(output_global));
    static_cast<void>(requests.write_string("wl_output"));
    static_cast<void>(requests.write_u32(4));
    static_cast<void>(requests.write_new_id(9));
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
    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 7 || capture.surface != 0) {
        return false;
    }

    wayland::protocol_writer commit;
    static_cast<void>(commit.begin_message(5, 1));
    static_cast<void>(commit.write_object(7));
    static_cast<void>(commit.write_i32(0));
    static_cast<void>(commit.write_i32(0));
    static_cast<void>(commit.end_message());
    static_cast<void>(commit.begin_message(5, 3));
    static_cast<void>(commit.write_new_id(8));
    static_cast<void>(commit.end_message());
    static_cast<void>(commit.begin_message(5, 6));
    static_cast<void>(commit.end_message());

    auto committed = client.send(commit.bytes(), commit.fds());
    if (!committed || committed->bytes != commit.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto commit_dispatched = server.dispatch_available();
    if (!commit_dispatched || *commit_dispatched != 3) {
        return false;
    }
    if (!server.flush()) {
        return false;
    }

    std::vector<std::byte> events;
    std::vector<pwm::handle_t> event_fds;
    if (!drain_client(client, events, event_fds)) {
        return false;
    }
    bool saw_enter = false;
    pwm::usize event_offset = 0;
    while (event_offset + sizeof(wayland::message_header) <= events.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{events}.subspan(event_offset));
        if (!header) {
            return false;
        }
        if (header->object == 5 && header->opcode == 0) {
            const auto payload = std::span<const std::byte>{events}.subspan(
                event_offset + sizeof(wayland::message_header),
                header->size - sizeof(wayland::message_header));
            wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
            auto event = generated::wl_surface::event_enter::decode(reader);
            if (!event) {
                return false;
            }
            saw_enter = std::get<0>(event->arguments) == 9;
        }
        event_offset += header->size;
    }

    return saw_enter && capture.surface == 5 && capture.buffer == 7 && capture.width == width &&
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

auto test_xdg_output() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-xdg-output.sock").string();
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
    const u32 manager_global = display.add_global("zxdg_output_manager_v1", 3);

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
    static_cast<void>(requests.write_u32(manager_global));
    static_cast<void>(requests.write_string("zxdg_output_manager_v1"));
    static_cast<void>(requests.write_u32(3));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 1));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.write_object(3));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched) {
        return false;
    }
    if (*dispatched != 4) {
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

    pwm::u32 xdg_events = 0;
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
        if (header->object == 5) {
            switch (header->opcode) {
            case 0:
                if (!generated::zxdg_output_v1::event_logical_position::decode(reader)) {
                    return false;
                }
                break;
            case 1:
                if (!generated::zxdg_output_v1::event_logical_size::decode(reader)) {
                    return false;
                }
                break;
            case 2:
                if (!generated::zxdg_output_v1::event_done::decode(reader)) {
                    return false;
                }
                break;
            case 3:
                if (!generated::zxdg_output_v1::event_name::decode(reader)) {
                    return false;
                }
                break;
            case 4:
                if (!generated::zxdg_output_v1::event_description::decode(reader)) {
                    return false;
                }
                break;
            default:
                return false;
            }
            ++xdg_events;
        } else if ((header->object == 2 && header->opcode == 0) || header->object == 3) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    return xdg_events == 5 && server.objects().find(5) != nullptr;
}

auto test_viewport_fractional_decoration() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-extra.sock").string();
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
    const u32 viewporter_global = display.add_global("wp_viewporter", 1);
    const u32 fractional_global = display.add_global("wp_fractional_scale_manager_v1", 1);
    const u32 decoration_global = display.add_global("zxdg_decoration_manager_v1", 1);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    const auto bind_global = [&requests](u32 global, std::string_view name, u32 version, u32 id) {
        static_cast<void>(requests.begin_message(2, 0));
        static_cast<void>(requests.write_u32(global));
        static_cast<void>(requests.write_string(name));
        static_cast<void>(requests.write_u32(version));
        static_cast<void>(requests.write_new_id(id));
        static_cast<void>(requests.end_message());
    };
    bind_global(viewporter_global, "wp_viewporter", 1, 3);
    bind_global(fractional_global, "wp_fractional_scale_manager_v1", 1, 4);
    bind_global(decoration_global, "zxdg_decoration_manager_v1", 1, 5);
    static_cast<void>(requests.begin_message(3, 1));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.write_object(100));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 1));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.write_object(100));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 1));
    static_cast<void>(requests.write_new_id(8));
    static_cast<void>(requests.write_object(100));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 7 || !server.flush()) {
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

    pwm::u32 fractional_events = 0;
    pwm::u32 decoration_events = 0;
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
        if (header->object == 7) {
            if (!generated::wp_fractional_scale_v1::event_preferred_scale::decode(reader)) {
                return false;
            }
            ++fractional_events;
        } else if (header->object == 8) {
            if (!generated::zxdg_toplevel_decoration_v1::event_configure::decode(reader)) {
                return false;
            }
            ++decoration_events;
        } else if (header->object == 2 && header->opcode == 0) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    return fractional_events == 1 && decoration_events == 1 &&
           server.objects().find(6) != nullptr && server.objects().find(7) != nullptr &&
           server.objects().find(8) != nullptr;
}

auto test_presentation() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-presentation.sock").string();
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
    const u32 presentation_global = display.add_global("wp_presentation", 1);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(presentation_global));
    static_cast<void>(requests.write_string("wp_presentation"));
    static_cast<void>(requests.write_u32(1));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(3, 1));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.write_object(100));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), {});
    if (!sent || sent->bytes != requests.bytes().size()) {
        return false;
    }
    if (!server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    if (!dispatched || *dispatched != 3 || !server.flush()) {
        return false;
    }

    std::array<std::byte, 1024> received{};
    auto result = client.receive(received, std::span<pwm::handle_t>{});
    if (!result) {
        return false;
    }
    const std::span<const std::byte> input{received.data(), result->bytes};
    pwm::usize offset = 0;
    pwm::u32 clock_events = 0;
    pwm::u32 presented_events = 0;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(input.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = input.subspan(offset + sizeof(wayland::message_header),
                                           header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 3 && header->opcode == 0) {
            if (!generated::wp_presentation::event_clock_id::decode(reader)) {
                return false;
            }
            ++clock_events;
        } else if (header->object == 4 && header->opcode == 1) {
            if (!generated::wp_presentation_feedback::event_presented::decode(reader)) {
                return false;
            }
            ++presented_events;
        } else if (header->object == 2 && header->opcode == 0) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    return clock_events == 1 && presented_events == 1;
}

struct clipboard_harness {
    wayland::display_service* source_display = nullptr;
    wayland::display_service* destination_display = nullptr;
    u32 offer = 0;
    bool selected = false;
    bool forwarded = false;
    bool accepted = false;
};

auto on_clipboard_selection(void* context, const wayland::data_source_event& event) -> void {
    auto& harness = *static_cast<clipboard_harness*>(context);
    harness.selected = true;
    auto offer = harness.destination_display->create_data_offer(1, event.source, event.mime_types,
                                                                event.actions);
    if (!offer) {
        return;
    }
    harness.offer = *offer;
    static_cast<void>(harness.destination_display->send_selection(*offer));
}

auto on_clipboard_receive(void* context, const wayland::data_offer_receive_event& event) -> void {
    auto& harness = *static_cast<clipboard_harness*>(context);
    harness.forwarded = static_cast<bool>(harness.source_display->send_data_source_send(
        event.source_object, event.mime_type, event.fd));
}

auto on_clipboard_accept(void* context, const wayland::data_offer_accept_event& event) -> void {
    auto& harness = *static_cast<clipboard_harness*>(context);
    harness.accepted = true;
    static_cast<void>(
        harness.source_display->send_data_source_target(event.source_object, event.mime_type));
}

auto drain_client(transport::stream& client, std::vector<std::byte>& input,
                  std::vector<pwm::handle_t>& fds) -> bool {
    std::array<std::byte, 4096> buffer{};
    std::array<pwm::handle_t, 8> fd_buffer{};
    for (i32 attempt = 0; attempt < 100; ++attempt) {
        auto result = client.receive(buffer, fd_buffer);
        if (!result) {
            return false;
        }
        input.insert(input.end(), buffer.begin(), buffer.begin() + result->bytes);
        fds.insert(fds.end(), fd_buffer.begin(), fd_buffer.begin() + result->fds);
        if (result->would_block || result->closed || (result->bytes == 0 && result->fds == 0)) {
            break;
        }
    }
    return true;
}

auto test_clipboard_transfer() -> bool {
    namespace transport = pwm::platform::wayland;
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-clipboard.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto source_client_result = transport::connect_stream(path);
    auto destination_client_result = transport::connect_stream(path);
    if (!source_client_result || !destination_client_result) {
        return false;
    }
    auto source_client = std::move(*source_client_result);
    auto destination_client = std::move(*destination_client_result);
    auto source_server_result = listener.accept();
    auto destination_server_result = listener.accept();
    if (!source_server_result || !destination_server_result) {
        return false;
    }

    wayland::connection source_server{std::move(*source_server_result)};
    wayland::connection destination_server{std::move(*destination_server_result)};
    wayland::display_service source_display{source_server};
    wayland::display_service destination_display{destination_server};
    if (!source_display.start() || !destination_display.start()) {
        return false;
    }
    const u32 source_seat = source_display.add_global("wl_seat", 2);
    const u32 source_manager = source_display.add_global("wl_data_device_manager", 3);
    const u32 destination_seat = destination_display.add_global("wl_seat", 2);
    const u32 destination_manager = destination_display.add_global("wl_data_device_manager", 3);

    clipboard_harness harness{
        .source_display = &source_display,
        .destination_display = &destination_display,
    };
    source_display.set_data_source_callback(on_clipboard_selection, &harness);
    destination_display.set_data_offer_receive_callback(on_clipboard_receive, &harness);
    destination_display.set_data_offer_accept_callback(on_clipboard_accept, &harness);

    const auto begin_registry = [](wayland::protocol_writer& writer) {
        static_cast<void>(writer.begin_message(1, 1));
        static_cast<void>(writer.write_new_id(2));
        static_cast<void>(writer.end_message());
    };
    const auto bind_global = [](wayland::protocol_writer& writer, u32 global,
                                std::string_view interface, u32 version, u32 id) {
        static_cast<void>(writer.begin_message(2, 0));
        static_cast<void>(writer.write_u32(global));
        static_cast<void>(writer.write_string(interface));
        static_cast<void>(writer.write_u32(version));
        static_cast<void>(writer.write_new_id(id));
        static_cast<void>(writer.end_message());
    };

    wayland::protocol_writer destination_setup;
    begin_registry(destination_setup);
    bind_global(destination_setup, destination_seat, "wl_seat", 2, 3);
    bind_global(destination_setup, destination_manager, "wl_data_device_manager", 3, 4);
    static_cast<void>(destination_setup.begin_message(4, 1));
    static_cast<void>(destination_setup.write_new_id(6));
    static_cast<void>(destination_setup.write_object(3));
    static_cast<void>(destination_setup.end_message());
    auto destination_sent =
        destination_client.send(destination_setup.bytes(), destination_setup.fds());
    if (!destination_sent || destination_sent->bytes != destination_setup.bytes().size()) {
        return false;
    }
    if (!destination_server.receive_available() || !destination_server.dispatch_available() ||
        !destination_server.flush()) {
        return false;
    }
    std::vector<std::byte> setup_input;
    std::vector<pwm::handle_t> setup_fds;
    if (!drain_client(destination_client, setup_input, setup_fds)) {
        return false;
    }

    wayland::protocol_writer source_setup;
    begin_registry(source_setup);
    bind_global(source_setup, source_seat, "wl_seat", 2, 3);
    bind_global(source_setup, source_manager, "wl_data_device_manager", 3, 4);
    static_cast<void>(source_setup.begin_message(4, 0));
    static_cast<void>(source_setup.write_new_id(5));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(4, 1));
    static_cast<void>(source_setup.write_new_id(7));
    static_cast<void>(source_setup.write_object(3));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(5, 0));
    static_cast<void>(source_setup.write_string("text/plain;charset=utf-8"));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(5, 0));
    static_cast<void>(source_setup.write_string("text/plain"));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(7, 1));
    static_cast<void>(source_setup.write_object(5));
    static_cast<void>(source_setup.write_u32(1));
    static_cast<void>(source_setup.end_message());

    auto source_sent = source_client.send(source_setup.bytes(), source_setup.fds());
    if (!source_sent || source_sent->bytes != source_setup.bytes().size()) {
        return false;
    }
    if (!source_server.receive_available() || !source_server.dispatch_available() ||
        !source_server.flush()) {
        return false;
    }
    std::vector<std::byte> source_setup_input;
    std::vector<pwm::handle_t> source_setup_fds;
    if (!drain_client(source_client, source_setup_input, source_setup_fds)) {
        return false;
    }
    if (!harness.selected || harness.offer == 0 || !destination_server.flush()) {
        return false;
    }

    std::vector<std::byte> destination_input;
    std::vector<pwm::handle_t> destination_fds;
    if (!drain_client(destination_client, destination_input, destination_fds)) {
        return false;
    }

    pwm::u32 offered_mime_types = 0;
    u32 selected_offer = 0;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= destination_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{destination_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{destination_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == harness.offer && header->opcode == 0) {
            if (!generated::wl_data_offer::event_offer::decode(reader)) {
                return false;
            }
            ++offered_mime_types;
        } else if (header->object == 6 && header->opcode == 5) {
            auto message = generated::wl_data_device::event_selection::decode(reader);
            if (!message) {
                return false;
            }
            selected_offer = std::get<0>(message->arguments);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (offered_mime_types != 2 || selected_offer != harness.offer) {
        return false;
    }

    auto pipe_result = transport::create_pipe();
    if (!pipe_result) {
        return false;
    }
    auto pipe = std::move(*pipe_result);
    wayland::protocol_writer transfer;
    static_cast<void>(transfer.begin_message(harness.offer, 0));
    static_cast<void>(transfer.write_u32(2));
    static_cast<void>(transfer.write_string("text/plain"));
    static_cast<void>(transfer.end_message());
    static_cast<void>(transfer.begin_message(harness.offer, 1));
    static_cast<void>(transfer.write_string("text/plain"));
    static_cast<void>(transfer.write_fd(pipe.write.fd()));
    static_cast<void>(transfer.end_message());
    auto transfer_sent = destination_client.send(transfer.bytes(), transfer.fds());
    pipe.write.destroy();
    if (!transfer_sent || transfer_sent->bytes != transfer.bytes().size()) {
        return false;
    }
    if (!destination_server.receive_available() || !destination_server.dispatch_available() ||
        !destination_server.flush()) {
        return false;
    }
    if (!harness.forwarded) {
        return false;
    }

    std::vector<std::byte> source_input;
    std::vector<pwm::handle_t> source_fds;
    if (!drain_client(source_client, source_input, source_fds) || source_fds.size() != 1) {
        return false;
    }

    bool saw_target = false;
    pwm::handle_t transfer_fd = -1;
    offset = 0;
    while (offset + sizeof(wayland::message_header) <= source_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{source_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{source_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, source_fds};
        if (header->object == 5 && header->opcode == 0) {
            if (!generated::wl_data_source::event_target::decode(reader)) {
                return false;
            }
            saw_target = true;
        } else if (header->object == 5 && header->opcode == 1) {
            auto message = generated::wl_data_source::event_send::decode(reader);
            if (!message) {
                return false;
            }
            transfer_fd = std::get<1>(message->arguments);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (!harness.accepted || !saw_target || transfer_fd < 0) {
        return false;
    }

    constexpr std::string_view payload_text = "powerwm-clipboard";
    auto transfer_owner = transport::file_descriptor::adopt(transfer_fd);
    const auto written = transfer_owner.write(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(payload_text.data()), payload_text.size()});
    transfer_owner.destroy();
    if (!written || *written != payload_text.size()) {
        return false;
    }

    std::string received_text;
    std::array<std::byte, 64> pipe_buffer{};
    while (true) {
        auto count = pipe.read.read(pipe_buffer);
        if (!count) {
            return false;
        }
        if (*count == 0) {
            break;
        }
        received_text.append(reinterpret_cast<const char*>(pipe_buffer.data()), *count);
    }
    return received_text == payload_text;
}

struct drag_harness {
    wayland::display_service* source_display = nullptr;
    wayland::display_service* destination_display = nullptr;
    wayland::connection* source_connection = nullptr;
    wayland::connection* destination_connection = nullptr;
    u32 source = 0;
    u32 offer = 0;
    bool started = false;
    bool acted = false;
    bool finished = false;
};

auto on_drag_start(void* context, const wayland::data_drag_event& event) -> void {
    auto& harness = *static_cast<drag_harness*>(context);
    harness.started = true;
    harness.source = event.source;
}

auto on_drag_actions(void* context, const wayland::data_offer_actions_event& event) -> void {
    auto& harness = *static_cast<drag_harness*>(context);
    harness.acted = true;
    static_cast<void>(
        harness.source_display->send_data_source_action(event.source_object, event.action));
}

auto on_drag_finished(void* context, const wayland::data_offer_finished_event& event) -> void {
    auto& harness = *static_cast<drag_harness*>(context);
    harness.finished = true;
    static_cast<void>(harness.source_display->send_data_source_dnd_finished(event.source_object));
}

auto test_drag_transfer() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-drag.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto source_client_result = transport::connect_stream(path);
    auto destination_client_result = transport::connect_stream(path);
    if (!source_client_result || !destination_client_result) {
        return false;
    }
    auto source_client = std::move(*source_client_result);
    auto destination_client = std::move(*destination_client_result);
    auto source_server_result = listener.accept();
    auto destination_server_result = listener.accept();
    if (!source_server_result || !destination_server_result) {
        return false;
    }
    wayland::connection source_server{std::move(*source_server_result)};
    wayland::connection destination_server{std::move(*destination_server_result)};
    wayland::display_service source_display{source_server};
    wayland::display_service destination_display{destination_server};
    if (!source_display.start() || !destination_display.start()) {
        return false;
    }
    source_display.add_global("wl_seat", 2);
    source_display.add_global("wl_data_device_manager", 3);
    destination_display.add_global("wl_seat", 2);
    destination_display.add_global("wl_data_device_manager", 3);

    drag_harness harness{
        .source_display = &source_display,
        .destination_display = &destination_display,
        .source_connection = &source_server,
        .destination_connection = &destination_server,
    };
    source_display.set_data_drag_callback(on_drag_start, &harness);
    destination_display.set_data_offer_actions_callback(on_drag_actions, &harness);
    destination_display.set_data_offer_finished_callback(on_drag_finished, &harness);

    const auto bind_global = [](wayland::protocol_writer& writer, u32 global,
                                std::string_view interface, u32 version, u32 id) {
        static_cast<void>(writer.begin_message(2, 0));
        static_cast<void>(writer.write_u32(global));
        static_cast<void>(writer.write_string(interface));
        static_cast<void>(writer.write_u32(version));
        static_cast<void>(writer.write_new_id(id));
        static_cast<void>(writer.end_message());
    };
    wayland::protocol_writer destination_setup;
    static_cast<void>(destination_setup.begin_message(1, 1));
    static_cast<void>(destination_setup.write_new_id(2));
    static_cast<void>(destination_setup.end_message());
    bind_global(destination_setup, 2, "wl_data_device_manager", 3, 4);
    static_cast<void>(destination_setup.begin_message(4, 1));
    static_cast<void>(destination_setup.write_new_id(6));
    static_cast<void>(destination_setup.write_object(3));
    static_cast<void>(destination_setup.end_message());
    auto destination_sent =
        destination_client.send(destination_setup.bytes(), destination_setup.fds());
    if (!destination_sent || !destination_server.receive_available() ||
        !destination_server.dispatch_available() || !destination_server.flush()) {
        return false;
    }
    std::vector<std::byte> ignored_input;
    std::vector<pwm::handle_t> ignored_fds;
    if (!drain_client(destination_client, ignored_input, ignored_fds)) {
        return false;
    }

    wayland::protocol_writer source_setup;
    static_cast<void>(source_setup.begin_message(1, 1));
    static_cast<void>(source_setup.write_new_id(2));
    static_cast<void>(source_setup.end_message());
    bind_global(source_setup, 2, "wl_data_device_manager", 3, 4);
    static_cast<void>(source_setup.begin_message(4, 0));
    static_cast<void>(source_setup.write_new_id(5));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(4, 1));
    static_cast<void>(source_setup.write_new_id(7));
    static_cast<void>(source_setup.write_object(3));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(5, 0));
    static_cast<void>(source_setup.write_string("text/plain"));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(5, 2));
    static_cast<void>(source_setup.write_u32(3));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(7, 0));
    static_cast<void>(source_setup.write_object(5));
    static_cast<void>(source_setup.write_object(0));
    static_cast<void>(source_setup.write_object(0));
    static_cast<void>(source_setup.write_u32(9));
    static_cast<void>(source_setup.end_message());
    auto source_sent = source_client.send(source_setup.bytes(), source_setup.fds());
    if (!source_sent || !source_server.receive_available() || !source_server.dispatch_available() ||
        !source_server.flush()) {
        return false;
    }
    std::vector<std::byte> source_setup_input;
    std::vector<pwm::handle_t> source_setup_fds;
    if (!drain_client(source_client, source_setup_input, source_setup_fds)) {
        return false;
    }
    if (!harness.started || harness.source != 5) {
        return false;
    }

    const std::array<std::string, 1> drag_mime_types{"text/plain"};
    auto offer = destination_display.create_data_offer(1, 5, drag_mime_types, 3);
    if (!offer) {
        return false;
    }
    harness.offer = *offer;
    if (!destination_display.send_data_offer_event(*offer) ||
        !destination_display.send_data_device_enter(11, 100, 5.0, 6.0, *offer) ||
        !destination_server.flush()) {
        return false;
    }

    std::vector<std::byte> destination_input;
    std::vector<pwm::handle_t> destination_fds;
    if (!drain_client(destination_client, destination_input, destination_fds)) {
        return false;
    }
    pwm::u32 offers = 0;
    pwm::u32 data_offers = 0;
    pwm::u32 enters = 0;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= destination_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{destination_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{destination_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == harness.offer && header->opcode == 0) {
            if (!generated::wl_data_offer::event_offer::decode(reader)) {
                return false;
            }
            ++offers;
        } else if (header->object == harness.offer && header->opcode == 1) {
            if (!generated::wl_data_offer::event_source_actions::decode(reader)) {
                return false;
            }
        } else if (header->object == 6 && header->opcode == 0) {
            if (!generated::wl_data_device::event_data_offer::decode(reader)) {
                return false;
            }
            ++data_offers;
        } else if (header->object == 6 && header->opcode == 1) {
            if (!generated::wl_data_device::event_enter::decode(reader)) {
                return false;
            }
            ++enters;
        } else {
            return false;
        }
        offset += header->size;
    }
    if (offers != 1 || data_offers != 1 || enters != 1) {
        return false;
    }

    wayland::protocol_writer finish;
    static_cast<void>(finish.begin_message(harness.offer, 4));
    static_cast<void>(finish.write_u32(3));
    static_cast<void>(finish.write_u32(1));
    static_cast<void>(finish.end_message());
    static_cast<void>(finish.begin_message(harness.offer, 3));
    static_cast<void>(finish.end_message());
    auto finish_sent = destination_client.send(finish.bytes(), finish.fds());
    if (!finish_sent || !destination_server.receive_available() ||
        !destination_server.dispatch_available() || !destination_server.flush()) {
        return false;
    }
    if (!harness.acted || !harness.finished) {
        return false;
    }
    static_cast<void>(source_display.send_data_source_dnd_drop_performed(5));
    if (!source_server.flush()) {
        return false;
    }

    std::vector<std::byte> source_input;
    std::vector<pwm::handle_t> source_fds;
    if (!drain_client(source_client, source_input, source_fds)) {
        return false;
    }
    pwm::u32 actions = 0;
    pwm::u32 drops = 0;
    pwm::u32 finishes = 0;
    offset = 0;
    while (offset + sizeof(wayland::message_header) <= source_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{source_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{source_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 5 && header->opcode == 5) {
            if (!generated::wl_data_source::event_action::decode(reader)) {
                return false;
            }
            ++actions;
        } else if (header->object == 5 && header->opcode == 3) {
            if (!generated::wl_data_source::event_dnd_drop_performed::decode(reader)) {
                return false;
            }
            ++drops;
        } else if (header->object == 5 && header->opcode == 4) {
            if (!generated::wl_data_source::event_dnd_finished::decode(reader)) {
                return false;
            }
            ++finishes;
        } else {
            return false;
        }
        offset += header->size;
    }
    return actions == 1 && drops == 1 && finishes == 1;
}

auto test_linux_dmabuf() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-dmabuf.sock").string();
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
    const u32 dmabuf_global = display.add_global("zwp_linux_dmabuf_v1", 3);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(dmabuf_global));
    static_cast<void>(requests.write_string("zwp_linux_dmabuf_v1"));
    static_cast<void>(requests.write_u32(3));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(3, 1));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || !server.receive_available() || !server.dispatch_available() || !server.flush()) {
        return false;
    }

    std::array<std::byte, 2048> received{};
    auto result = client.receive(received, std::span<pwm::handle_t>{});
    if (!result) {
        return false;
    }
    const std::span<const std::byte> input{received.data(), result->bytes};
    pwm::u32 formats = 0;
    pwm::u32 modifiers = 0;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(input.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = input.subspan(offset + sizeof(wayland::message_header),
                                           header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 3 && header->opcode == 0) {
            if (!generated::zwp_linux_dmabuf_v1::event_format::decode(reader)) {
                return false;
            }
            ++formats;
        } else if (header->object == 3 && header->opcode == 1) {
            if (!generated::zwp_linux_dmabuf_v1::event_modifier::decode(reader)) {
                return false;
            }
            ++modifiers;
        } else if (header->object == 2 && header->opcode == 0) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (formats != 2 || modifiers != 1) {
        return false;
    }

    auto file = pwm::platform::wayland::create_anonymous_file("powerwm-dmabuf", 32 * 32 * 4);
    if (!file) {
        return false;
    }
    wayland::protocol_writer create;
    static_cast<void>(create.begin_message(4, 1));
    static_cast<void>(create.write_fd(file->fd()));
    static_cast<void>(create.write_u32(0));
    static_cast<void>(create.write_u32(0));
    static_cast<void>(create.write_u32(128));
    static_cast<void>(create.write_u32(0));
    static_cast<void>(create.write_u32(0));
    static_cast<void>(create.end_message());
    static_cast<void>(create.begin_message(4, 3));
    static_cast<void>(create.write_new_id(5));
    static_cast<void>(create.write_i32(32));
    static_cast<void>(create.write_i32(32));
    static_cast<void>(create.write_u32(0x34325241));
    static_cast<void>(create.write_u32(0));
    static_cast<void>(create.end_message());
    auto created = client.send(create.bytes(), create.fds());
    if (!created || !server.receive_available() || !server.dispatch_available() ||
        !server.flush()) {
        return false;
    }
    if (server.objects().find(4) == nullptr || server.objects().find(5) == nullptr) {
        return false;
    }

    wayland::protocol_writer destroy;
    static_cast<void>(destroy.begin_message(5, 0));
    static_cast<void>(destroy.end_message());
    static_cast<void>(destroy.begin_message(4, 0));
    static_cast<void>(destroy.end_message());
    auto destroyed = client.send(destroy.bytes(), destroy.fds());
    if (!destroyed || !server.receive_available() || !server.dispatch_available()) {
        return false;
    }
    return server.objects().find(4) == nullptr && server.objects().find(5) == nullptr;
}

auto test_layer_shell() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-layer.sock").string();
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
    const u32 layer_global = display.add_global("zwlr_layer_shell_v1", 4);

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    const auto bind_global = [&requests](u32 global, std::string_view interface, u32 version,
                                         u32 id) {
        static_cast<void>(requests.begin_message(2, 0));
        static_cast<void>(requests.write_u32(global));
        static_cast<void>(requests.write_string(interface));
        static_cast<void>(requests.write_u32(version));
        static_cast<void>(requests.write_new_id(id));
        static_cast<void>(requests.end_message());
    };
    bind_global(compositor_global, "wl_compositor", 4, 3);
    bind_global(layer_global, "zwlr_layer_shell_v1", 4, 4);
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 0));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.write_object(5));
    static_cast<void>(requests.write_object(0));
    static_cast<void>(requests.write_u32(2));
    static_cast<void>(requests.write_string("powerwm-test"));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 0));
    static_cast<void>(requests.write_u32(200));
    static_cast<void>(requests.write_u32(60));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 1));
    static_cast<void>(requests.write_u32(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 2));
    static_cast<void>(requests.write_i32(50));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 3));
    static_cast<void>(requests.write_i32(4));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(0));
    static_cast<void>(requests.write_i32(6));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(5, 6));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || !server.receive_available() || !server.dispatch_available() || !server.flush()) {
        return false;
    }

    std::array<std::byte, 1024> received{};
    auto result = client.receive(received, std::span<pwm::handle_t>{});
    if (!result) {
        return false;
    }
    const std::span<const std::byte> input{received.data(), result->bytes};
    pwm::u32 serial = 0;
    pwm::u32 width = 0;
    pwm::u32 height = 0;
    pwm::usize offset = 0;
    bool saw_configure = false;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(input.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = input.subspan(offset + sizeof(wayland::message_header),
                                           header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 6 && header->opcode == 0) {
            auto event = generated::zwlr_layer_surface_v1::event_configure::decode(reader);
            if (!event) {
                return false;
            }
            serial = std::get<0>(event->arguments);
            width = std::get<1>(event->arguments);
            height = std::get<2>(event->arguments);
            saw_configure = true;
        } else if (header->object == 2 && header->opcode == 0) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (!saw_configure || width != 200 || height != 60 || serial == 0) {
        return false;
    }

    const auto geometry = display.layer_geometry(5, 1280, 720, 0, 0);
    if (!geometry || geometry->bounds.x != 6 || geometry->bounds.y != 4 ||
        geometry->bounds.width != 200 || geometry->bounds.height != 60 ||
        geometry->keyboard_interactive) {
        return false;
    }

    wayland::protocol_writer acknowledge;
    static_cast<void>(acknowledge.begin_message(6, 6));
    static_cast<void>(acknowledge.write_u32(serial));
    static_cast<void>(acknowledge.end_message());
    static_cast<void>(acknowledge.begin_message(6, 7));
    static_cast<void>(acknowledge.end_message());
    auto acknowledged = client.send(acknowledge.bytes(), acknowledge.fds());
    if (!acknowledged || !server.receive_available() || !server.dispatch_available()) {
        return false;
    }
    return server.objects().find(6) == nullptr;
}

struct fuzz_region_implementation {
    auto request_add(i32 x, i32 y, i32 width, i32 height) -> pwm::result<void> {
        static_cast<void>(x);
        static_cast<void>(y);
        static_cast<void>(width);
        static_cast<void>(height);
        return {};
    }

    auto request_subtract(i32 x, i32 y, i32 width, i32 height) -> pwm::result<void> {
        static_cast<void>(x);
        static_cast<void>(y);
        static_cast<void>(width);
        static_cast<void>(height);
        return {};
    }

    auto request_destroy() -> pwm::result<void> {
        return {};
    }
};

auto test_wire_fuzz() -> bool {
    wayland::object_table table;
    fuzz_region_implementation region;
    if (!wayland::add_object<fuzz_region_implementation, generated::wl_region>(table, 3, region)) {
        return false;
    }

    u32 state = 0x12345678;
    const auto next = [&state]() {
        state = state * 1664525U + 1013904223U;
        return state;
    };
    std::array<std::byte, 256> storage{};
    for (i32 iteration = 0; iteration < 20000; ++iteration) {
        const auto length = static_cast<pwm::usize>(next() % storage.size());
        for (pwm::usize index = 0; index < length; ++index) {
            storage[index] = static_cast<std::byte>(next() & 0xFFU);
        }
        if (length >= sizeof(wayland::message_header)) {
            static_cast<void>(
                wayland::decode_header(std::span<const std::byte>{storage}.first(length)));
        }
        const auto opcode = static_cast<u16>(next() % 16U);
        wayland::protocol_reader reader{std::span<const std::byte>{storage}.first(length),
                                        std::span<const pwm::handle_t>{}};
        static_cast<void>(table.dispatch(3, opcode, reader));
    }

    constexpr std::string_view base =
        R"(<?xml version="1.0"?><protocol name="fuzz"><interface name="fuzz" version="1"><request name="do"><arg name="v" type="uint"/></request></interface></protocol>)";
    std::string document;
    for (i32 iteration = 0; iteration < 2000; ++iteration) {
        document.assign(base);
        const auto edits = next() % 8U;
        for (u32 edit = 0; edit < edits && !document.empty(); ++edit) {
            const auto index = static_cast<pwm::usize>(next() % document.size());
            document[index] = static_cast<char>(next() & 0x7FU);
        }
        auto tree = wayland::parse_xml(document);
        if (tree) {
            static_cast<void>(wayland::build_protocol(*tree));
        }
    }
    return true;
}

auto test_frame_callback_deferral() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-frame.sock").string();
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
    display.set_defer_frame_callbacks(true);

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
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(4));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 3));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 6));
    static_cast<void>(requests.end_message());
    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || !server.receive_available() || !server.dispatch_available() || !server.flush()) {
        return false;
    }

    std::vector<std::byte> initial_input;
    std::vector<pwm::handle_t> initial_fds;
    if (!drain_client(client, initial_input, initial_fds)) {
        return false;
    }
    std::array<std::byte, 1024> received{};
    auto pending = client.receive(received, std::span<pwm::handle_t>{});
    if (!pending || !pending->would_block || pending->bytes != 0) {
        return false;
    }
    if (auto flushed = display.flush_frame_callbacks(); !flushed || !server.flush()) {
        return false;
    }
    auto result = client.receive(received, std::span<pwm::handle_t>{});
    if (!result || result->bytes == 0) {
        return false;
    }
    const std::span<const std::byte> input{received.data(), result->bytes};
    pwm::usize offset = 0;
    bool saw_done = false;
    while (offset + sizeof(wayland::message_header) <= input.size()) {
        auto header = wayland::decode_header(input.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = input.subspan(offset + sizeof(wayland::message_header),
                                           header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == 5 && header->opcode == 0) {
            if (!generated::wl_callback::event_done::decode(reader)) {
                return false;
            }
            saw_done = true;
        } else if (header->object == 1 && header->opcode == 1) {
            static_cast<void>(reader);
        } else {
            return false;
        }
        offset += header->size;
    }
    return saw_done;
}

struct primary_harness {
    wayland::display_service* source_display = nullptr;
    wayland::display_service* destination_display = nullptr;
    u32 offer = 0;
    bool selected = false;
    bool forwarded = false;
};

auto on_primary_selection(void* context, const wayland::primary_source_event& event) -> void {
    auto& harness = *static_cast<primary_harness*>(context);
    harness.selected = true;
    auto offer =
        harness.destination_display->create_primary_offer(1, event.source, event.mime_types);
    if (!offer) {
        return;
    }
    harness.offer = *offer;
    static_cast<void>(harness.destination_display->send_primary_selection(*offer));
}

auto on_primary_receive(void* context, const wayland::primary_offer_receive_event& event) -> void {
    auto& harness = *static_cast<primary_harness*>(context);
    harness.forwarded = static_cast<bool>(harness.source_display->send_primary_source_send(
        event.source_object, event.mime_type, event.fd));
}

auto test_primary_selection() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-primary.sock").string();
    auto listener_result = transport::listener::create(path);
    if (!listener_result) {
        return false;
    }
    auto listener = std::move(*listener_result);
    auto source_client_result = transport::connect_stream(path);
    auto destination_client_result = transport::connect_stream(path);
    if (!source_client_result || !destination_client_result) {
        return false;
    }
    auto source_client = std::move(*source_client_result);
    auto destination_client = std::move(*destination_client_result);
    auto source_server_result = listener.accept();
    auto destination_server_result = listener.accept();
    if (!source_server_result || !destination_server_result) {
        return false;
    }
    wayland::connection source_server{std::move(*source_server_result)};
    wayland::connection destination_server{std::move(*destination_server_result)};
    wayland::display_service source_display{source_server};
    wayland::display_service destination_display{destination_server};
    if (!source_display.start() || !destination_display.start()) {
        return false;
    }
    const u32 source_global =
        source_display.add_global("zwp_primary_selection_device_manager_v1", 1);
    const u32 destination_global =
        destination_display.add_global("zwp_primary_selection_device_manager_v1", 1);

    primary_harness harness{
        .source_display = &source_display,
        .destination_display = &destination_display,
    };
    source_display.set_primary_source_callback(on_primary_selection, &harness);
    destination_display.set_primary_offer_receive_callback(on_primary_receive, &harness);

    const auto bind_global = [](wayland::protocol_writer& writer, u32 global,
                                std::string_view interface, u32 version, u32 id) {
        static_cast<void>(writer.begin_message(2, 0));
        static_cast<void>(writer.write_u32(global));
        static_cast<void>(writer.write_string(interface));
        static_cast<void>(writer.write_u32(version));
        static_cast<void>(writer.write_new_id(id));
        static_cast<void>(writer.end_message());
    };

    wayland::protocol_writer destination_setup;
    static_cast<void>(destination_setup.begin_message(1, 1));
    static_cast<void>(destination_setup.write_new_id(2));
    static_cast<void>(destination_setup.end_message());
    bind_global(destination_setup, destination_global, "zwp_primary_selection_device_manager_v1", 1,
                3);
    static_cast<void>(destination_setup.begin_message(3, 1));
    static_cast<void>(destination_setup.write_new_id(5));
    static_cast<void>(destination_setup.write_object(0));
    static_cast<void>(destination_setup.end_message());
    auto destination_sent =
        destination_client.send(destination_setup.bytes(), destination_setup.fds());
    if (!destination_sent || !destination_server.receive_available() ||
        !destination_server.dispatch_available() || !destination_server.flush()) {
        return false;
    }
    std::vector<std::byte> ignored_input;
    std::vector<pwm::handle_t> ignored_fds;
    if (!drain_client(destination_client, ignored_input, ignored_fds)) {
        return false;
    }

    wayland::protocol_writer source_setup;
    static_cast<void>(source_setup.begin_message(1, 1));
    static_cast<void>(source_setup.write_new_id(2));
    static_cast<void>(source_setup.end_message());
    bind_global(source_setup, source_global, "zwp_primary_selection_device_manager_v1", 1, 3);
    static_cast<void>(source_setup.begin_message(3, 0));
    static_cast<void>(source_setup.write_new_id(4));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(4, 0));
    static_cast<void>(source_setup.write_string("text/plain"));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(3, 1));
    static_cast<void>(source_setup.write_new_id(6));
    static_cast<void>(source_setup.write_object(0));
    static_cast<void>(source_setup.end_message());
    static_cast<void>(source_setup.begin_message(6, 0));
    static_cast<void>(source_setup.write_object(4));
    static_cast<void>(source_setup.write_u32(1));
    static_cast<void>(source_setup.end_message());
    auto source_sent = source_client.send(source_setup.bytes(), source_setup.fds());
    if (!source_sent || !source_server.receive_available() || !source_server.dispatch_available() ||
        !source_server.flush()) {
        return false;
    }
    std::vector<std::byte> setup_input;
    std::vector<pwm::handle_t> setup_fds;
    if (!drain_client(source_client, setup_input, setup_fds)) {
        return false;
    }
    if (!harness.selected || harness.offer == 0 || !destination_server.flush()) {
        return false;
    }

    std::vector<std::byte> destination_input;
    std::vector<pwm::handle_t> destination_fds;
    if (!drain_client(destination_client, destination_input, destination_fds)) {
        return false;
    }
    pwm::u32 offers = 0;
    u32 selected = 0;
    pwm::usize offset = 0;
    while (offset + sizeof(wayland::message_header) <= destination_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{destination_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{destination_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, std::span<const pwm::handle_t>{}};
        if (header->object == harness.offer && header->opcode == 0) {
            if (!generated::zwp_primary_selection_offer_v1::event_offer::decode(reader)) {
                return false;
            }
            ++offers;
        } else if (header->object == 5 && header->opcode == 1) {
            auto event =
                generated::zwp_primary_selection_device_v1::event_selection::decode(reader);
            if (!event) {
                return false;
            }
            selected = std::get<0>(event->arguments);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (offers != 1 || selected != harness.offer) {
        return false;
    }

    auto pipe_result = transport::create_pipe();
    if (!pipe_result) {
        return false;
    }
    auto pipe = std::move(*pipe_result);
    wayland::protocol_writer transfer;
    static_cast<void>(transfer.begin_message(harness.offer, 0));
    static_cast<void>(transfer.write_string("text/plain"));
    static_cast<void>(transfer.write_fd(pipe.write.fd()));
    static_cast<void>(transfer.end_message());
    auto transfer_sent = destination_client.send(transfer.bytes(), transfer.fds());
    pipe.write.destroy();
    if (!transfer_sent || !destination_server.receive_available() ||
        !destination_server.dispatch_available() || !destination_server.flush()) {
        return false;
    }
    if (!harness.forwarded) {
        return false;
    }

    std::vector<std::byte> source_input;
    std::vector<pwm::handle_t> source_fds;
    if (!drain_client(source_client, source_input, source_fds) || source_fds.size() != 1) {
        return false;
    }
    pwm::handle_t transfer_fd = -1;
    offset = 0;
    while (offset + sizeof(wayland::message_header) <= source_input.size()) {
        auto header =
            wayland::decode_header(std::span<const std::byte>{source_input}.subspan(offset));
        if (!header) {
            return false;
        }
        const auto payload = std::span<const std::byte>{source_input}.subspan(
            offset + sizeof(wayland::message_header),
            header->size - sizeof(wayland::message_header));
        wayland::protocol_reader reader{payload, source_fds};
        if (header->object == 4 && header->opcode == 0) {
            auto event = generated::zwp_primary_selection_source_v1::event_send::decode(reader);
            if (!event) {
                return false;
            }
            transfer_fd = std::get<1>(event->arguments);
        } else {
            return false;
        }
        offset += header->size;
    }
    if (transfer_fd < 0) {
        return false;
    }

    constexpr std::string_view payload_text = "powerwm-primary";
    auto transfer_owner = transport::file_descriptor::adopt(transfer_fd);
    const auto written = transfer_owner.write(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(payload_text.data()), payload_text.size()});
    transfer_owner.destroy();
    if (!written || *written != payload_text.size()) {
        return false;
    }

    std::string received_text;
    std::array<std::byte, 64> pipe_buffer{};
    while (true) {
        auto count = pipe.read.read(pipe_buffer);
        if (!count) {
            return false;
        }
        if (*count == 0) {
            break;
        }
        received_text.append(reinterpret_cast<const char*>(pipe_buffer.data()), *count);
    }
    return received_text == payload_text;
}

auto test_object_validation() -> bool {
    wayland::object_table table;
    display_implementation implementation;
    if (wayland::add_object<display_implementation, generated::wl_display>(table, 0,
                                                                           implementation)) {
        return false;
    }
    if (wayland::add_object<display_implementation, generated::wl_display>(table, 0xff000000U,
                                                                           implementation)) {
        return false;
    }
    if (!wayland::add_object<display_implementation, generated::wl_display>(table, 2,
                                                                            implementation)) {
        return false;
    }

    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-validation.sock").string();
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

    wayland::protocol_writer requests;
    static_cast<void>(requests.begin_message(1, 1));
    static_cast<void>(requests.write_new_id(2));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(2, 0));
    static_cast<void>(requests.write_u32(compositor_global));
    static_cast<void>(requests.write_string("wl_compositor"));
    static_cast<void>(requests.write_u32(5));
    static_cast<void>(requests.write_new_id(3));
    static_cast<void>(requests.end_message());
    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || !server.receive_available()) {
        return false;
    }
    auto dispatched = server.dispatch_available();
    return !dispatched && dispatched.error().code == pwm::error_code::protocol;
}

auto test_popup_positioning() -> bool {
    const auto path =
        (std::filesystem::temp_directory_path() / "powerwm-wayland-popup.sock").string();
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
    const auto bind_global = [&requests](u32 global, std::string_view interface, u32 version,
                                         u32 id) {
        static_cast<void>(requests.begin_message(2, 0));
        static_cast<void>(requests.write_u32(global));
        static_cast<void>(requests.write_string(interface));
        static_cast<void>(requests.write_u32(version));
        static_cast<void>(requests.write_new_id(id));
        static_cast<void>(requests.end_message());
    };
    bind_global(compositor_global, "wl_compositor", 4, 3);
    bind_global(xdg_global, "xdg_wm_base", 1, 4);
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 2));
    static_cast<void>(requests.write_new_id(6));
    static_cast<void>(requests.write_object(5));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(6, 1));
    static_cast<void>(requests.write_new_id(7));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 1));
    static_cast<void>(requests.write_new_id(8));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(8, 1));
    static_cast<void>(requests.write_i32(100));
    static_cast<void>(requests.write_i32(50));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(8, 2));
    static_cast<void>(requests.write_i32(10));
    static_cast<void>(requests.write_i32(20));
    static_cast<void>(requests.write_i32(30));
    static_cast<void>(requests.write_i32(40));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(8, 3));
    static_cast<void>(requests.write_u32(8));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(8, 4));
    static_cast<void>(requests.write_u32(1));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(3, 0));
    static_cast<void>(requests.write_new_id(9));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(4, 2));
    static_cast<void>(requests.write_new_id(10));
    static_cast<void>(requests.write_object(9));
    static_cast<void>(requests.end_message());
    static_cast<void>(requests.begin_message(10, 2));
    static_cast<void>(requests.write_new_id(11));
    static_cast<void>(requests.write_object(6));
    static_cast<void>(requests.write_object(8));
    static_cast<void>(requests.end_message());

    auto sent = client.send(requests.bytes(), requests.fds());
    if (!sent || !server.receive_available() || !server.dispatch_available() || !server.flush()) {
        return false;
    }

    std::vector<std::byte> input;
    std::vector<pwm::handle_t> fds;
    if (!drain_client(client, input, fds)) {
        return false;
    }
    pwm::i32 x = 0;
    pwm::i32 y = 0;
    pwm::i32 width = 0;
    pwm::i32 height = 0;
    bool saw_popup_configure = false;
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
        if (header->object == 11 && header->opcode == 0) {
            auto event = generated::xdg_popup::event_configure::decode(reader);
            if (!event) {
                return false;
            }
            x = std::get<0>(event->arguments);
            y = std::get<1>(event->arguments);
            width = std::get<2>(event->arguments);
            height = std::get<3>(event->arguments);
            saw_popup_configure = true;
        }
        offset += header->size;
    }
    if (!saw_popup_configure || x != 40 || y != 20 || width != 100 || height != 50) {
        return false;
    }
    const auto placement = display.surface_popup_geometry(9);
    return placement && placement->parent_surface == 5 && placement->bounds.x == 40 &&
           placement->bounds.y == 20 && placement->bounds.width == 100 &&
           placement->bounds.height == 50;
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
    if (!test_popup_positioning()) {
        std::println(stderr, "popup positioning test failed");
        return EXIT_FAILURE;
    }
    if (!test_object_validation()) {
        std::println(stderr, "object validation test failed");
        return EXIT_FAILURE;
    }
    if (!test_primary_selection()) {
        std::println(stderr, "primary selection test failed");
        return EXIT_FAILURE;
    }
    if (!test_frame_callback_deferral()) {
        std::println(stderr, "frame callback deferral test failed");
        return EXIT_FAILURE;
    }
    if (!test_wire_fuzz()) {
        std::println(stderr, "wire fuzz test failed");
        return EXIT_FAILURE;
    }
    if (!test_layer_shell()) {
        std::println(stderr, "layer shell test failed");
        return EXIT_FAILURE;
    }
    if (!test_linux_dmabuf()) {
        std::println(stderr, "linux-dmabuf test failed");
        return EXIT_FAILURE;
    }
    if (!test_drag_transfer()) {
        std::println(stderr, "drag transfer test failed");
        return EXIT_FAILURE;
    }
    if (!test_clipboard_transfer()) {
        std::println(stderr, "clipboard transfer test failed");
        return EXIT_FAILURE;
    }
    if (!test_presentation()) {
        std::println(stderr, "presentation test failed");
        return EXIT_FAILURE;
    }
    if (!test_viewport_fractional_decoration()) {
        std::println(stderr, "viewport/fractional/decoration test failed");
        return EXIT_FAILURE;
    }
    if (!test_xdg_output()) {
        std::println(stderr, "xdg-output test failed");
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
