// SPDX-License-Identifier: Apache-2.0

#include <wayland/protocol.hpp>

#include <charconv>
#include <utility>

namespace pwm::wayland {

namespace {

auto parse_u32(std::string_view text, u32 fallback) -> u32 {
    if (text.empty()) {
        return fallback;
    }
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return fallback;
    }
    return value;
}

auto parse_bool(std::string_view text) -> bool {
    return text == "true" || text == "1";
}

auto parse_argument(const xml_node& node) -> protocol_argument {
    return {
        .name = std::string{node.attribute("name")},
        .type = std::string{node.attribute("type")},
        .interface = std::string{node.attribute("interface")},
        .enum_name = std::string{node.attribute("enum")},
        .allow_null = parse_bool(node.attribute("allow-null")),
    };
}

auto parse_message(const xml_node& node) -> protocol_message {
    protocol_message message{
        .name = std::string{node.attribute("name")},
        .since = parse_u32(node.attribute("since"), 1),
        .destructor = node.attribute("type") == "destructor",
    };
    for (const xml_node& child : node.children) {
        if (child.name == "arg") {
            message.arguments.push_back(parse_argument(child));
        }
    }
    return message;
}

auto parse_enum_entry(const xml_node& node) -> protocol_enum_entry {
    return {
        .name = std::string{node.attribute("name")},
        .value = parse_u32(node.attribute("value"), 0),
        .since = parse_u32(node.attribute("since"), 1),
    };
}

auto parse_enum(const xml_node& node) -> protocol_enum {
    protocol_enum result{
        .name = std::string{node.attribute("name")},
        .since = parse_u32(node.attribute("since"), 1),
        .bitfield = parse_bool(node.attribute("bitfield")),
    };
    for (const xml_node& child : node.children) {
        if (child.name == "entry") {
            result.entries.push_back(parse_enum_entry(child));
        }
    }
    return result;
}

auto parse_interface(const xml_node& node) -> protocol_interface {
    protocol_interface result{
        .name = std::string{node.attribute("name")},
        .version = parse_u32(node.attribute("version"), 1),
    };
    for (const xml_node& child : node.children) {
        if (child.name == "request") {
            result.requests.push_back(parse_message(child));
        } else if (child.name == "event") {
            result.events.push_back(parse_message(child));
        } else if (child.name == "enum") {
            result.enums.push_back(parse_enum(child));
        }
    }
    return result;
}

}

auto build_protocol(const xml_node& root) noexcept -> result<protocol> {
    if (root.name != "protocol") {
        return std::unexpected(
            make_error(error_code::protocol, "root XML element is not a protocol"));
    }

    protocol result{.name = std::string{root.attribute("name")}};
    if (result.name.empty()) {
        return std::unexpected(make_error(error_code::protocol, "protocol has no name"));
    }

    for (const xml_node& child : root.children) {
        if (child.name != "interface") {
            continue;
        }
        protocol_interface parsed = parse_interface(child);
        if (parsed.name.empty()) {
            return std::unexpected(make_error(error_code::protocol, "interface has no name"));
        }
        result.interfaces.push_back(std::move(parsed));
    }
    return result;
}

}
