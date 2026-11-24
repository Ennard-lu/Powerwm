// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/raii.hpp>
#include <base/types.hpp>
#include <platform/drm/device.hpp>

#include <cstring>
#include <map>
#include <meta>
#include <span>
#include <string>
#include <string_view>

namespace pwm::platform::drm {

using property_id_map = std::map<std::string, u32>;

auto lookup_property_ids(const device& device, property_set properties,
                         std::span<const std::string_view> names, property_id_map&& result = {})
    -> property_id_map;

template <typename T>
inline auto lookup_property_ids(const device& device, property_set properties) -> T {
    T result{};
    for (const u32 property_id : properties.property_ids()) {
        auto property = raii{device.get_property_info(property_id)};
        template for (constexpr auto member :
                      std::define_static_array(std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::current()))) {
            const auto property_name = [:std::meta::constant_of(
                                             std::meta::annotations_of(member).front()):];
            if (std::string_view{property_name} == property->name()) {
                result.[:member:] = property_id;
            }
        }
    }
    return result;
}

}