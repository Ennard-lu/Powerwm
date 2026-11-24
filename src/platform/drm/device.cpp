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
    if (drmSetMaster(fd_handle_) != 0) {
        return std::unexpected(make_error(error_code::permission_denied, "drmSetMaster failed"));
    }
    return {};
}

auto device::drop_master() const noexcept -> result<void> {
    if (drmDropMaster(fd_handle_) != 0) {
        return std::unexpected(make_error(error_code::io, "drmDropMaster failed"));
    }
    return {};
}

auto device::set_universal_planes_enabled(bool enabled) const noexcept -> result<void> {
    if (drmSetClientCap(fd_handle_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, enabled ? 1 : 0) != 0) {
        return std::unexpected(
            make_error(error_code::unsupported, "DRM_CLIENT_CAP_UNIVERSAL_PLANES failed"));
    }
    return {};
}

auto device::set_atomic_enabled(bool enabled) const noexcept -> result<void> {
    if (drmSetClientCap(fd_handle_, DRM_CLIENT_CAP_ATOMIC, enabled ? 1 : 0) != 0) {
        return std::unexpected(make_error(error_code::unsupported, "DRM_CLIENT_CAP_ATOMIC failed"));
    }
    return {};
}

auto device::get_resources() const noexcept -> device_resources {
    return device_resources::wrap(drmModeGetResources(fd_handle_));
}

auto device::get_plane_resources() const noexcept -> plane_set {
    return plane_set::wrap(drmModeGetPlaneResources(fd_handle_));
}

auto device::get_property_info(u32 property_id) const noexcept -> property_info {
    return property_info::wrap(drmModeGetProperty(fd_handle_, property_id));
}

auto device::get_connector_properties(u32 connector_id) const noexcept -> property_set {
    return property_set::wrap(
        drmModeObjectGetProperties(fd_handle_, connector_id, DRM_MODE_OBJECT_CONNECTOR));
}

auto device::get_crtc_properties(u32 crtc_id) const noexcept -> property_set {
    return property_set::wrap(
        drmModeObjectGetProperties(fd_handle_, crtc_id, DRM_MODE_OBJECT_CRTC));
}

auto device::get_plane_properties(u32 plane_id) const noexcept -> property_set {
    return property_set::wrap(
        drmModeObjectGetProperties(fd_handle_, plane_id, DRM_MODE_OBJECT_PLANE));
}

auto device::commit_atomic(const atomic_request& request, u32 flags) const noexcept
    -> result<void> {
    if (drmModeAtomicCommit(fd_handle_, request.handle, flags, nullptr) != 0) {
        return std::unexpected(make_error(error_code::io, "drmModeAtomicCommit failed"));
    }
    return {};
}

auto device::get_crtc(u32 crtc_id) const noexcept -> crtc {
    return crtc::wrap(drmModeGetCrtc(fd_handle_, crtc_id));
}

auto device::get_connector(u32 connector_id) const noexcept -> connector {
    return connector::wrap(drmModeGetConnector(fd_handle_, connector_id));
}

auto device::create_dumb_buffer(const buffer_format& format) const noexcept -> result<dumb_buffer> {
    drm_mode_create_dumb create_info{
        .height = format.height_px,
        .width = format.width_px,
        .bpp = format.bpp,
    };

    if (drmIoctl(fd_handle_, DRM_IOCTL_MODE_CREATE_DUMB, &create_info) != 0) {
        return std::unexpected(make_error(error_code::io, "DRM_IOCTL_MODE_CREATE_DUMB failed"));
    }

    return dumb_buffer{
        fd_handle_,
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

auto device::create_framebuffer(const dumb_buffer& buffer) const noexcept -> result<framebuffer> {
    u32 id = 0;
    if (drmModeAddFB(fd_handle_, buffer.format().width_px, buffer.format().height_px,
                     buffer.format().depth_bits, buffer.format().bpp, buffer.format().pitch_bytes,
                     buffer.handle(), &id) != 0 ||
        id == 0) {
        return std::unexpected(make_error(error_code::io, "drmModeAddFB failed"));
    }
    return framebuffer{fd_handle_, id};
}

auto device::create_property_blob(std::span<const std::byte> data) const noexcept
    -> result<property_blob> {
    u32 id = 0;
    if (drmModeCreatePropertyBlob(fd_handle_, data.data(), data.size(), &id) != 0 || id == 0) {
        return std::unexpected(make_error(error_code::io, "drmModeCreatePropertyBlob failed"));
    }
    return property_blob{fd_handle_, id};
}

auto device::destroy() const noexcept -> void {
    ::close(fd_handle_);
}

auto dumb_buffer::map() const noexcept -> result<mapped_memory> {
    drm_mode_map_dumb map_info{.handle = handle_};
    if (drmIoctl(device_fd_, DRM_IOCTL_MODE_MAP_DUMB, &map_info) != 0) {
        return std::unexpected(make_error(error_code::io, "DRM_IOCTL_MODE_MAP_DUMB failed"));
    }

    void* address = ::mmap(nullptr, format_.size_bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
                           device_fd_, map_info.offset);
    if (address == MAP_FAILED) {
        return std::unexpected(make_error(error_code::io, "mmap failed"));
    }
    return mapped_memory{address, format_.size_bytes};
}

auto dumb_buffer::destroy() const noexcept -> void {
    drm_mode_destroy_dumb destroy_info{.handle = handle_};
    drmIoctl(device_fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_info);
}

auto property_blob::destroy() const noexcept -> void {
    drmModeDestroyPropertyBlob(device_fd_, id_);
}

auto framebuffer::destroy() const noexcept -> void {
    drmModeRmFB(device_fd_, id_);
}

}