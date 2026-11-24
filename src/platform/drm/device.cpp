// SPDX-License-Identifier: Apache-2.0

#include <base/raii.hpp>
#include <platform/drm/device.hpp>
#include <platform/event/poll.hpp>

#include <cerrno>
#include <string>
#include <system_error>

namespace pwm::platform::drm {

auto device::create(const fs::path& path) noexcept -> result<device> {
    const handle_t fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(make_error(error_code::io, "open DRM device failed"));
    }
    return device{fd};
}

auto device::create_borrowed(handle_t fd) noexcept -> device {
    return device{fd, false};
}

auto device::open_first_available() noexcept -> result<device> {
    std::error_code error;
    std::filesystem::directory_iterator iterator{"/dev/dri", error};
    if (error) {
        return std::unexpected(make_error(error_code::not_found, "cannot open /dev/dri"));
    }

    for (const auto& entry : iterator) {
        const std::string name = entry.path().filename().string();
        if (!name.starts_with("card")) {
            continue;
        }
        auto device = create(entry.path());
        if (device) {
            return device;
        }
    }

    return std::unexpected(make_error(error_code::not_found, "no DRM card device found"));
}

auto device::is_master() const noexcept -> bool {
    return drmIsMaster(fd_handle_) == 1;
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
        return std::unexpected(make_error(error_code::io, "drmModeAtomicCommit failed",
                                          std::source_location::current(), errno));
    }
    return {};
}

auto device::get_crtc(u32 crtc_id) const noexcept -> crtc {
    return crtc::wrap(drmModeGetCrtc(fd_handle_, crtc_id));
}

auto device::get_connector(u32 connector_id) const noexcept -> connector {
    return connector::wrap(drmModeGetConnector(fd_handle_, connector_id));
}

auto device::find_primary_output() const noexcept -> result<output_target> {
    auto resources = raii{get_resources()};
    if (!resources->valid() || resources->crtc_ids().empty()) {
        return std::unexpected(make_error(error_code::not_found, "no DRM CRTC available"));
    }

    auto planes = raii{get_plane_resources()};
    if (!planes->valid() || planes->plane_ids().empty()) {
        return std::unexpected(make_error(error_code::not_found, "no DRM plane available"));
    }

    for (const u32 connector_id : resources->connector_ids()) {
        auto connector = raii{get_connector(connector_id)};
        if (!connector->valid() || !connector->is_connected() || connector->mode_count() == 0) {
            continue;
        }
        return output_target{
            .crtc_id = resources->crtc_ids().front(),
            .connector_id = connector_id,
            .plane_id = planes->plane_ids().front(),
            .mode = connector->mode(0),
        };
    }

    return std::unexpected(make_error(error_code::not_found, "no connected DRM output"));
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

auto device::create_framebuffer_from_dma_buf(handle_t dma_buf_fd, u32 width_px, u32 height_px,
                                             u32 pitch_bytes, u32 format,
                                             u64 modifier) const noexcept
    -> result<dma_buf_framebuffer> {
    u32 gem_handle = 0;
    if (drmPrimeFDToHandle(fd_handle_, dma_buf_fd, &gem_handle) != 0) {
        return std::unexpected(make_error(error_code::io, "drmPrimeFDToHandle failed"));
    }

    u32 handles[1]{gem_handle};
    u32 pitches[1]{pitch_bytes};
    u32 offsets[1]{0};
    u64 modifiers[1]{modifier};
    u32 fb_id = 0;

    if (drmModeAddFB2WithModifiers(fd_handle_, width_px, height_px, format, handles, pitches,
                                   offsets, modifiers, &fb_id, DRM_MODE_FB_MODIFIERS) != 0) {
        const i32 modifier_error = errno;
        if (drmModeAddFB2(fd_handle_, width_px, height_px, format, handles, pitches, offsets,
                          &fb_id, 0) == 0) {
            return dma_buf_framebuffer{fd_handle_, fb_id, gem_handle};
        }
        drmCloseBufferHandle(fd_handle_, gem_handle);
        return std::unexpected(make_error(error_code::io, "drmModeAddFB2 failed",
                                          std::source_location::current(), modifier_error));
    }
    return dma_buf_framebuffer{fd_handle_, fb_id, gem_handle};
}

auto device::create_property_blob(std::span<const std::byte> data) const noexcept
    -> result<property_blob> {
    u32 id = 0;
    if (drmModeCreatePropertyBlob(fd_handle_, data.data(), data.size(), &id) != 0 || id == 0) {
        return std::unexpected(make_error(error_code::io, "drmModeCreatePropertyBlob failed"));
    }
    return property_blob{fd_handle_, id};
}

namespace {

device::page_flip_callback active_page_flip_callback = nullptr;
void* active_page_flip_context = nullptr;

auto dispatch_page_flip(int, unsigned int, unsigned int, unsigned int, void*) -> void {
    if (active_page_flip_callback != nullptr) {
        active_page_flip_callback(active_page_flip_context);
    }
}

auto mark_page_flip(void* user_data) -> void {
    *static_cast<bool*>(user_data) = true;
}

}

auto device::wait_for_page_flip(i32 timeout_ms) const noexcept -> result<void> {
    bool completed = false;

    while (!completed) {
        auto readable = event::wait_readable(fd_handle_, timeout_ms);
        if (!readable) {
            return std::unexpected(readable.error());
        }
        if (!*readable) {
            return std::unexpected(make_error(error_code::io, "page flip timed out"));
        }
        if (auto handled = handle_events(mark_page_flip, &completed); !handled) {
            return std::unexpected(handled.error());
        }
    }
    return {};
}

auto device::handle_events(page_flip_callback callback, void* context) const noexcept
    -> result<void> {
    active_page_flip_callback = callback;
    active_page_flip_context = context;
    drmEventContext event_context{};
    event_context.version = DRM_EVENT_CONTEXT_VERSION;
    if (callback != nullptr) {
        event_context.page_flip_handler = dispatch_page_flip;
    }

    if (drmHandleEvent(fd_handle_, &event_context) != 0) {
        return std::unexpected(make_error(error_code::io, "drmHandleEvent failed"));
    }
    return {};
}

auto device::destroy() const noexcept -> void {
    if (owns_fd_) {
        ::close(fd_handle_);
    }
    fd_handle_ = -1;
    owns_fd_ = false;
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

auto dma_buf_framebuffer::valid() const noexcept -> bool {
    return fb_id_ != 0;
}

auto dma_buf_framebuffer::id() const noexcept -> u32 {
    return fb_id_;
}

auto dma_buf_framebuffer::destroy() const noexcept -> void {
    drmModeRmFB(device_fd_, fb_id_);
    drmCloseBufferHandle(device_fd_, gem_handle_);
    fb_id_ = 0;
}

}