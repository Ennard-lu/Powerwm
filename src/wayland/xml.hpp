// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <base/types.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace pwm::wayland {

struct xml_attribute {
    std::string name;
    std::string value;
};

struct xml_node {
    std::string name;
    std::vector<xml_attribute> attributes;
    std::vector<xml_node> children;
    std::string text;

    [[nodiscard]] auto attribute(std::string_view key) const noexcept -> std::string_view;
};

[[nodiscard]] auto parse_xml(std::string_view input) noexcept -> result<xml_node>;

}
