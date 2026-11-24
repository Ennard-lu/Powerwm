// SPDX-License-Identifier: Apache-2.0

#include <platform/drm/lookup.hpp>

namespace pwm::platform::drm {

auto lookup_property_ids(const device& device, property_set properties,
                         std::span<const std::string_view> names, property_id_map&& result)
    -> property_id_map {
    for (const u32 property_id : properties.property_ids()) {
        auto property = raii{device.get_property_info(property_id)};
        for (const auto& name : names) {
            if (property->name() == name) {
                result[std::string{name}] = property_id;
            }
        }
    }
    return result;
}

}