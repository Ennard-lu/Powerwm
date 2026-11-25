// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/result.hpp>
#include <wayland/dispatch.hpp>
#include <wayland/wire.hpp>

#include <string_view>
#include <vector>

namespace pwm::wayland {

using object_dispatch = auto (*)(void* implementation, u16 opcode, protocol_reader& reader)
    -> result<void>;

struct object_entry {
    object_id id = 0;
    std::string_view interface_name;
    u32 version = 1;
    void* implementation = nullptr;
    object_dispatch dispatch = nullptr;
};

class object_table {
  public:
    [[nodiscard]] auto add(object_id id, std::string_view interface_name, u32 version,
                           void* implementation, object_dispatch dispatch,
                           bool server_created = false) noexcept -> result<void>;

    [[nodiscard]] auto remove(object_id id) noexcept -> bool;

    [[nodiscard]] auto find(object_id id) noexcept -> object_entry*;

    [[nodiscard]] auto find(object_id id) const noexcept -> const object_entry*;

    [[nodiscard]] auto dispatch(object_id id, u16 opcode, protocol_reader& reader) noexcept
        -> result<void>;

    [[nodiscard]] auto size() const noexcept -> usize;

    auto clear() noexcept -> void;

  private:
    std::vector<object_entry> entries_;
};

template <typename Implementation, typename Interface>
[[nodiscard]] auto add_object(object_table& table, object_id id, Implementation& implementation,
                              bool server_created = false) noexcept -> result<void> {
    return table.add(
        id, Interface::name, Interface::version, &implementation,
        [](void* opaque, u16 opcode, protocol_reader& reader) -> result<void> {
            return dispatch_request<Implementation, Interface>(
                *static_cast<Implementation*>(opaque), opcode, reader);
        },
        server_created);
}

}
