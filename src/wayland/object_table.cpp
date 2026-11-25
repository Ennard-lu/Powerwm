// SPDX-License-Identifier: Apache-2.0

#include <wayland/object_table.hpp>

#include <algorithm>

namespace pwm::wayland {

auto object_table::add(object_id id, std::string_view interface_name, u32 version,
                       void* implementation, object_dispatch dispatch, bool server_created) noexcept
    -> result<void> {
    if (id == 0 || implementation == nullptr || dispatch == nullptr) {
        return std::unexpected(
            make_error(error_code::invalid_argument, "object entry arguments are invalid"));
    }
    if (!server_created && id >= 0xff000000U) {
        return std::unexpected(
            make_error(error_code::protocol, "client object id is in the server range"));
    }
    if (find(id) != nullptr) {
        return std::unexpected(make_error(error_code::protocol, "object id is already registered"));
    }
    entries_.push_back(object_entry{
        .id = id,
        .interface_name = interface_name,
        .version = version,
        .implementation = implementation,
        .dispatch = dispatch,
    });
    return {};
}

auto object_table::remove(object_id id) noexcept -> bool {
    const auto iterator =
        std::ranges::find_if(entries_, [id](const object_entry& entry) { return entry.id == id; });
    if (iterator == entries_.end()) {
        return false;
    }
    entries_.erase(iterator);
    return true;
}

auto object_table::find(object_id id) noexcept -> object_entry* {
    const auto iterator =
        std::ranges::find_if(entries_, [id](const object_entry& entry) { return entry.id == id; });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

auto object_table::find(object_id id) const noexcept -> const object_entry* {
    const auto iterator =
        std::ranges::find_if(entries_, [id](const object_entry& entry) { return entry.id == id; });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

auto object_table::dispatch(object_id id, u16 opcode, protocol_reader& reader) noexcept
    -> result<void> {
    object_entry* entry = find(id);
    if (entry == nullptr) {
        return std::unexpected(
            make_error(error_code::protocol, "request targets an unknown object"));
    }
    return entry->dispatch(entry->implementation, opcode, reader);
}

auto object_table::size() const noexcept -> usize {
    return entries_.size();
}

auto object_table::clear() noexcept -> void {
    entries_.clear();
}

}
