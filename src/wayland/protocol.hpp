// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>
#include <wayland/xml.hpp>

#include <string>
#include <vector>

namespace pwm::wayland {

struct protocol_argument {
    std::string name;
    std::string type;
    std::string interface;
    std::string enum_name;
    bool allow_null = false;
};

struct protocol_message {
    std::string name;
    u32 since = 1;
    bool destructor = false;
    std::vector<protocol_argument> arguments;
};

struct protocol_enum_entry {
    std::string name;
    u32 value = 0;
    u32 since = 1;
};

struct protocol_enum {
    std::string name;
    u32 since = 1;
    bool bitfield = false;
    std::vector<protocol_enum_entry> entries;
};

struct protocol_interface {
    std::string name;
    u32 version = 1;
    std::vector<protocol_message> requests;
    std::vector<protocol_message> events;
    std::vector<protocol_enum> enums;
};

struct protocol {
    std::string name;
    std::vector<protocol_interface> interfaces;
};

[[nodiscard]] auto build_protocol(const xml_node& root) noexcept -> result<protocol>;

}
