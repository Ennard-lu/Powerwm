// SPDX-License-Identifier: Apache-2.0

#include <platform/drm/device.hpp>

namespace pwm::platform::drm {

auto device::create(const fs::path& path) noexcept -> result<device> {
    const handle_t fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(make_error(error_code::io, "open DRM device failed"));
    }
    return device{fd};
}

auto device::set_master() const noexcept -> result<void> {
    if (drmSetMaster(fd_handle) != 0) {
        return std::unexpected(make_error(error_code::permission_denied, "drmSetMaster failed"));
    }
    return {};
}

auto device::drop_master() const noexcept -> result<void> {
    if (drmDropMaster(fd_handle) != 0) {
        return std::unexpected(make_error(error_code::io, "drmDropMaster failed"));
    }
    return {};
}

auto device::set_universal_planes_enabled(bool enabled) const noexcept -> result<void> {
    if (drmSetClientCap(fd_handle, DRM_CLIENT_CAP_UNIVERSAL_PLANES, enabled ? 1 : 0) != 0) {
        return std::unexpected(
            make_error(error_code::unsupported, "DRM_CLIENT_CAP_UNIVERSAL_PLANES failed"));
    }
    return {};
}

auto device::set_atomic_enabled(bool enabled) const noexcept -> result<void> {
    if (drmSetClientCap(fd_handle, DRM_CLIENT_CAP_ATOMIC, enabled ? 1 : 0) != 0) {
        return std::unexpected(make_error(error_code::unsupported, "DRM_CLIENT_CAP_ATOMIC failed"));
    }
    return {};
}

device_resources device::get_resources() const noexcept {
    return device_resources::wrap(drmModeGetResources(fd_handle));
}

plane_set device::get_plane_resources() const noexcept {
    return plane_set::wrap(drmModeGetPlaneResources(fd_handle));
}

property_info device::get_property_info(u32 property_id) const noexcept {
    return property_info::wrap(drmModeGetProperty(fd_handle, property_id));
}

property_set device::get_connector_properties(u32 connector_id) const noexcept {
    return property_set::wrap(
        drmModeObjectGetProperties(fd_handle, connector_id, DRM_MODE_OBJECT_CONNECTOR));
}

property_set device::get_crtc_properties(u32 crtc_id) const noexcept {
    return property_set::wrap(drmModeObjectGetProperties(fd_handle, crtc_id, DRM_MODE_OBJECT_CRTC));
}

property_set device::get_plane_properties(u32 plane_id) const noexcept {
    return property_set::wrap(
        drmModeObjectGetProperties(fd_handle, plane_id, DRM_MODE_OBJECT_PLANE));
}

auto device::commit_atomic(const atomic_request& request, u32 flags) const noexcept
    -> result<void> {
    if (drmModeAtomicCommit(fd_handle, request.handle, flags, nullptr) != 0) {
        return std::unexpected(make_error(error_code::io, "drmModeAtomicCommit failed"));
    }
    return {};
}

crtc device::get_crtc(u32 crtc_id) const noexcept {
    return crtc::wrap(drmModeGetCrtc(fd_handle, crtc_id));
}

connector device::get_connector(u32 connector_id) const noexcept {
    return connector::wrap(drmModeGetConnector(fd_handle, connector_id));
}

auto device::create_property_blob(std::span<const std::byte> data) const noexcept
    -> result<property_blob> {
    u32 id = 0;
    if (drmModeCreatePropertyBlob(fd_handle, data.data(), data.size(), &id) != 0 || id == 0) {
        return std::unexpected(make_error(error_code::io, "drmModeCreatePropertyBlob failed"));
    }
    return property_blob{id};
}

void device::destroy() const noexcept {
    ::close(fd_handle);
}

auto dumb_buffer::create(const device& device, const buffer_format& format) noexcept
    -> result<dumb_buffer> {
    drm_mode_create_dumb create_info{
        .height = format.height_px,
        .width = format.width_px,
        .bpp = format.bpp,
    };

    if (drmIoctl(device.fd(), DRM_IOCTL_MODE_CREATE_DUMB, &create_info) != 0) {
        return std::unexpected(make_error(error_code::io, "DRM_IOCTL_MODE_CREATE_DUMB failed"));
    }

    return dumb_buffer{
        create_info.handle,
        buffer_format{
            .width_px = create_info.width,
            .height_px = create_info.height,
            .pitch_bytes = create_info.pitch,
            .depth_bits = format.depth_bits,
            .bpp = format.bpp,
            .size_bytes = create_info.size,
        },
    };
}

void dumb_buffer::destroy(handle_t device_fd) const noexcept {
    drm_mode_destroy_dumb destroy_info{.handle = handle};
    drmIoctl(device_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_info);
}

auto dumb_buffer::map(handle_t device_fd, usize size_bytes) const noexcept
    -> result<mapped_memory> {
    drm_mode_map_dumb map_info{.handle = handle};
    if (drmIoctl(device_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_info) != 0) {
        return std::unexpected(make_error(error_code::io, "DRM_IOCTL_MODE_MAP_DUMB failed"));
    }

    void* address =
        ::mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, device_fd, map_info.offset);
    if (address == MAP_FAILED) {
        return std::unexpected(make_error(error_code::io, "mmap failed"));
    }
    return mapped_memory{address, size_bytes};
}

void property_blob::destroy(handle_t device_fd) const noexcept {
    drmModeDestroyPropertyBlob(device_fd, id);
}

auto framebuffer::create(const device& device, const dumb_buffer& buffer) noexcept
    -> result<framebuffer> {
    u32 id = 0;
    if (drmModeAddFB(device.fd(), buffer.format.width_px, buffer.format.height_px,
                     buffer.format.depth_bits, buffer.format.bpp, buffer.format.pitch_bytes,
                     buffer.handle, &id) != 0 ||
        id == 0) {
        return std::unexpected(make_error(error_code::io, "drmModeAddFB failed"));
    }
    return framebuffer{id};
}

void framebuffer::destroy(handle_t device_fd) const noexcept {
    drmModeRmFB(device_fd, id);
}

}