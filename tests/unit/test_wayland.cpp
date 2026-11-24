// SPDX-License-Identifier: Apache-2.0

#include <base/result.hpp>
#include <base/types.hpp>
#include <wayland/dispatch.hpp>
#include <wayland/object_table.hpp>
#include <wayland/protocol.hpp>
#include <wayland/wire.hpp>
#include <wayland/xml.hpp>

#include <array>
#include <cstdlib>
#include <print>
#include <span>
#include <string_view>
#include <tuple>
#include <wayland_bindings.hpp>
#include <xdg_shell_bindings.hpp>

namespace {

namespace wayland = pwm::wayland;
namespace generated = pwm::wayland::generated;

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
    if (!test_generated_descriptors()) {
        std::println(stderr, "generated descriptor test failed");
        return EXIT_FAILURE;
    }
    std::println("wayland protocol tests passed");
    return EXIT_SUCCESS;
}
