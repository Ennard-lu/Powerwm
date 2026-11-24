// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/macros.hpp>
#include <base/result.hpp>
#include <base/types.hpp>
#include <platform/drm/buffer.hpp>

#include <drm.h>
#include <drm_mode.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace pwm::platform::drm {

inline constexpr u32 atomic_allow_modeset = DRM_MODE_ATOMIC_ALLOW_MODESET;

namespace fs = std::filesystem;

struct device_resources {
    drmModeRes* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] std::span<u32> crtc_ids() const noexcept {
        return {handle->crtcs, static_cast<usize>(handle->count_crtcs)};
    }

    [[nodiscard]] std::span<u32> connector_ids() const noexcept {
        return {handle->connectors, static_cast<usize>(handle->count_connectors)};
    }

    [[nodiscard]] std::span<u32> framebuffer_ids() const noexcept {
        return {handle->fbs, static_cast<usize>(handle->count_fbs)};
    }

    [[nodiscard]] std::span<u32> encoder_ids() const noexcept {
        return {handle->encoders, static_cast<usize>(handle->count_encoders)};
    }

    [[nodiscard]] static device_resources wrap(drmModeRes* resources) noexcept {
        return {resources};
    }

    void destroy() const noexcept {
        drmModeFreeResources(handle);
    }
};

class display_mode {
  public:
    display_mode() noexcept = default;

    explicit display_mode(const drmModeModeInfo& mode) noexcept : mode_(mode) {
    }

    [[nodiscard]] u32 width_px() const noexcept {
        return mode_.hdisplay;
    }

    [[nodiscard]] u32 height_px() const noexcept {
        return mode_.vdisplay;
    }

    [[nodiscard]] u32 refresh_hz() const noexcept {
        return mode_.vrefresh;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return {reinterpret_cast<const std::byte*>(&mode_), sizeof(mode_)};
    }

  private:
    drmModeModeInfo mode_{};
};

struct connector {
    drmModeConnector* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] u32 mode_count() const noexcept {
        return static_cast<u32>(handle->count_modes);
    }

    [[nodiscard]] display_mode mode(u32 index) const noexcept {
        return display_mode{handle->modes[index]};
    }

    [[nodiscard]] std::span<u32> property_ids() const noexcept {
        return {handle->props, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] std::span<u32> encoder_ids() const noexcept {
        return {handle->encoders, static_cast<usize>(handle->count_encoders)};
    }

    [[nodiscard]] static connector wrap(drmModeConnector* connector) noexcept {
        return {connector};
    }

    void destroy() const noexcept {
        drmModeFreeConnector(handle);
    }
};

struct crtc {
    drmModeCrtc* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] static crtc wrap(drmModeCrtc* crtc) noexcept {
        return {crtc};
    }

    void destroy() const noexcept {
        drmModeFreeCrtc(handle);
    }
};

struct property_set {
    drmModeObjectProperties* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] std::span<u32> property_ids() const noexcept {
        return {handle->props, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] std::span<u64> property_values() const noexcept {
        return {handle->prop_values, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] static property_set wrap(drmModeObjectProperties* properties) noexcept {
        return {properties};
    }

    void destroy() const noexcept {
        drmModeFreeObjectProperties(handle);
    }
};

struct atomic_request {
    drmModeAtomicReq* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] static auto create() noexcept -> result<atomic_request> {
        drmModeAtomicReq* request = drmModeAtomicAlloc();
        if (request == nullptr) {
            return std::unexpected(
                make_error(error_code::resource_exhausted, "drmModeAtomicAlloc returned null"));
        }
        return atomic_request{request};
    }

    [[nodiscard]] bool add_property(u32 object_id, u32 property_id, u64 value) const noexcept {
        return drmModeAtomicAddProperty(handle, object_id, property_id, value) == 0;
    }

    [[nodiscard]] bool merge(const atomic_request& other) const noexcept {
        return drmModeAtomicMerge(handle, other.handle) == 0;
    }

    [[nodiscard]] atomic_request duplicate() const noexcept {
        return {drmModeAtomicDuplicate(handle)};
    }

    void destroy() const noexcept {
        drmModeAtomicFree(handle);
    }
};

struct property_info {
    drmModePropertyRes* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] std::string_view name() const noexcept {
        return handle->name;
    }

    [[nodiscard]] static property_info wrap(drmModePropertyRes* property) noexcept {
        return {property};
    }

    void destroy() const noexcept {
        drmModeFreeProperty(handle);
    }
};

struct plane_set {
    drmModePlaneRes* handle = nullptr;

    [[nodiscard]] bool valid() const noexcept {
        return handle != nullptr;
    }

    [[nodiscard]] std::span<const u32> plane_ids() const noexcept {
        return {handle->planes, static_cast<usize>(handle->count_planes)};
    }

    [[nodiscard]] static plane_set wrap(drmModePlaneRes* planes) noexcept {
        return {planes};
    }

    void destroy() const noexcept {
        drmModeFreePlaneResources(handle);
    }
};

struct property_blob;
struct framebuffer;

struct device {
    handle_t fd_handle = -1;

    device() noexcept = default;

    explicit device(handle_t fd) noexcept : fd_handle(fd) {
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_handle >= 0;
    }

    [[nodiscard]] handle_t fd() const noexcept {
        return fd_handle;
    }

    [[nodiscard]] static auto create(const fs::path& path) noexcept -> result<device>;

    [[nodiscard]] auto set_master() const noexcept -> result<void>;

    [[nodiscard]] auto drop_master() const noexcept -> result<void>;

    [[nodiscard]] auto set_universal_planes_enabled(bool enabled) const noexcept -> result<void>;

    [[nodiscard]] auto set_atomic_enabled(bool enabled) const noexcept -> result<void>;

    [[nodiscard]] device_resources get_resources() const noexcept;

    [[nodiscard]] plane_set get_plane_resources() const noexcept;

    [[nodiscard]] property_info get_property_info(u32 property_id) const noexcept;

    [[nodiscard]] property_set get_connector_properties(u32 connector_id) const noexcept;

    [[nodiscard]] property_set get_crtc_properties(u32 crtc_id) const noexcept;

    [[nodiscard]] property_set get_plane_properties(u32 plane_id) const noexcept;

    [[nodiscard]] auto commit_atomic(const atomic_request& request, u32 flags) const noexcept
        -> result<void>;

    [[nodiscard]] crtc get_crtc(u32 crtc_id) const noexcept;

    [[nodiscard]] connector get_connector(u32 connector_id) const noexcept;

    [[nodiscard]] auto create_property_blob(std::span<const std::byte> data) const noexcept
        -> result<property_blob>;

    void destroy() const noexcept;
};

struct mapped_memory {
    void* address = MAP_FAILED;
    usize size_bytes = 0;

    [[nodiscard]] bool valid() const noexcept {
        return address != MAP_FAILED;
    }

    void destroy() const noexcept {
        ::munmap(address, size_bytes);
    }
};

struct dumb_buffer {
    u32 handle = 0;
    buffer_format format{};

    [[nodiscard]] bool valid() const noexcept {
        return handle != 0;
    }

    [[nodiscard]] static auto create(const device& device, const buffer_format& format) noexcept
        -> result<dumb_buffer>;

    void destroy(handle_t device_fd) const noexcept;

    [[nodiscard]] auto map(handle_t device_fd, usize size_bytes) const noexcept
        -> result<mapped_memory>;
};

struct property_blob {
    u32 id = 0;

    [[nodiscard]] bool valid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] static property_blob wrap(u32 id) noexcept {
        return {id};
    }

    void destroy(handle_t device_fd) const noexcept;
};

struct framebuffer {
    u32 id = 0;

    [[nodiscard]] bool valid() const noexcept {
        return id != 0;
    }

    [[nodiscard]] static auto create(const device& device, const dumb_buffer& buffer) noexcept
        -> result<framebuffer>;

    void destroy(handle_t device_fd) const noexcept;
};

}