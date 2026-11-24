// SPDX-License-Identifier: Apache-2.0

#include <platform/udev/monitor.hpp>

#include <libudev.h>

#include <string_view>

namespace {

auto device_string(const char* value) -> std::string {
    return value != nullptr ? std::string{value} : std::string{};
}

auto device_hotplug(udev_device* device) -> bool {
    const char* value = udev_device_get_property_value(device, "HOTPLUG");
    return value != nullptr && std::string_view{value} == "1";
}

}

namespace pwm::platform::udev {

auto monitor::create() noexcept -> result<monitor> {
    struct udev* context = udev_new();
    if (context == nullptr) {
        return std::unexpected(make_error(error_code::resource_exhausted, "udev_new failed"));
    }

    udev_monitor* handle = udev_monitor_new_from_netlink(context, "udev");
    if (handle == nullptr) {
        udev_unref(context);
        return std::unexpected(make_error(error_code::io, "udev monitor creation failed"));
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(handle, "drm", nullptr) < 0 ||
        udev_monitor_enable_receiving(handle) < 0) {
        udev_monitor_unref(handle);
        udev_unref(context);
        return std::unexpected(make_error(error_code::io, "udev monitor setup failed"));
    }

    const handle_t fd = udev_monitor_get_fd(handle);
    if (fd < 0) {
        udev_monitor_unref(handle);
        udev_unref(context);
        return std::unexpected(make_error(error_code::io, "udev monitor fd failed"));
    }
    return monitor{context, handle, fd};
}

auto monitor::valid() const noexcept -> bool {
    return context_ != nullptr && handle_ != nullptr && fd_ >= 0;
}

auto monitor::fd() const noexcept -> handle_t {
    return fd_;
}

auto monitor::receive() noexcept -> std::optional<device_event> {
    udev_device* device = udev_monitor_receive_device(static_cast<udev_monitor*>(handle_));
    if (device == nullptr) {
        return std::nullopt;
    }

    device_event event{
        .action = device_string(udev_device_get_action(device)),
        .subsystem = device_string(udev_device_get_subsystem(device)),
        .devnode = device_string(udev_device_get_devnode(device)),
        .hotplug = device_hotplug(device),
    };
    udev_device_unref(device);
    return event;
}

auto monitor::destroy() const noexcept -> void {
    udev_monitor_unref(static_cast<udev_monitor*>(handle_));
    udev_unref(static_cast<struct udev*>(context_));
    handle_ = nullptr;
    context_ = nullptr;
    fd_ = -1;
}

}