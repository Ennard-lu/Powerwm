// SPDX-License-Identifier: Apache-2.0

#include <wayland/display.hpp>

#include <memory>
#include <tuple>
#include <utility>
#include <wayland_bindings.hpp>

namespace pwm::wayland {

namespace {

auto queue_event(connection& target, const protocol_writer& writer) -> result<void> {
    return target.queue_message(writer);
}

}

display_service::display_service(connection& connection) noexcept
    : connection_(&connection), display_{this} {
}

auto display_service::start() -> result<void> {
    return add_object<display_object, generated::wl_display>(connection_->objects(), 1, display_);
}

auto display_service::add_global(std::string_view interface, u32 version) -> u32 {
    const u32 name = next_global_name_++;
    globals_.push_back(registry_global{
        .name = name,
        .interface = std::string{interface},
        .version = version,
    });
    return name;
}

auto display_service::globals() const noexcept -> const std::vector<registry_global>& {
    return globals_;
}

auto display_service::next_serial() noexcept -> u32 {
    return ++next_serial_;
}

auto display_service::send_registry_globals(object_id registry) -> result<void> {
    for (const auto& global : globals_) {
        generated::wl_registry::event_global event{};
        std::get<0>(event.arguments) = global.name;
        std::get<1>(event.arguments) = global.interface;
        std::get<2>(event.arguments) = global.version;

        protocol_writer writer;
        if (auto encoded = generated::wl_registry::event_global::encode(writer, registry, event);
            !encoded) {
            return std::unexpected(encoded.error());
        }
        if (auto queued = queue_event(*connection_, writer); !queued) {
            return std::unexpected(queued.error());
        }
    }
    return {};
}

auto display_service::send_callback_done(object_id callback, u32 data) -> result<void> {
    generated::wl_callback::event_done done{};
    std::get<0>(done.arguments) = data;

    protocol_writer done_writer;
    if (auto encoded = generated::wl_callback::event_done::encode(done_writer, callback, done);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    if (auto queued = queue_event(*connection_, done_writer); !queued) {
        return std::unexpected(queued.error());
    }

    generated::wl_display::event_delete_id deleted{};
    std::get<0>(deleted.arguments) = callback;

    protocol_writer delete_writer;
    if (auto encoded = generated::wl_display::event_delete_id::encode(delete_writer, 1, deleted);
        !encoded) {
        return std::unexpected(encoded.error());
    }
    return queue_event(*connection_, delete_writer);
}

auto display_object::request_sync(object_id callback) -> result<void> {
    return service->send_callback_done(callback, service->next_serial());
}

auto display_object::request_get_registry(object_id registry) -> result<void> {
    auto object = std::make_unique<registry_object>(registry_object{service, registry});
    if (auto added = add_object<registry_object, generated::wl_registry>(
            service->connection_->objects(), registry, *object);
        !added) {
        return std::unexpected(added.error());
    }
    service->registries_.push_back(std::move(object));
    return service->send_registry_globals(registry);
}

auto registry_object::request_bind(u32 name, std::string_view interface, u32 version,
                                   object_id new_id) -> result<void> {
    static_cast<void>(new_id);
    static_cast<void>(version);
    for (const auto& global : service->globals_) {
        if (global.name == name && global.interface == interface) {
            return std::unexpected(
                make_error(error_code::unsupported, "global binding is not implemented"));
        }
    }
    return std::unexpected(
        make_error(error_code::protocol, "registry bind references an unknown global"));
}

}
