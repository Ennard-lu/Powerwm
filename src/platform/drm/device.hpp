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
inline constexpr u32 atomic_nonblock = DRM_MODE_ATOMIC_NONBLOCK;
inline constexpr u32 atomic_page_flip_event = DRM_MODE_PAGE_FLIP_EVENT;

namespace fs = std::filesystem;

class device;

class display_mode {
  public:
    display_mode() noexcept = default;

    explicit display_mode(const drmModeModeInfo& mode) noexcept : mode_(mode) {
    }

    [[nodiscard]] auto width_px() const noexcept -> u32 {
        return mode_.hdisplay;
    }

    [[nodiscard]] auto height_px() const noexcept -> u32 {
        return mode_.vdisplay;
    }

    [[nodiscard]] auto refresh_hz() const noexcept -> u32 {
        return mode_.vrefresh;
    }

    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte> {
        return {reinterpret_cast<const std::byte*>(&mode_), sizeof(mode_)};
    }

  private:
    drmModeModeInfo mode_{};
};

struct device_resources {
    drmModeRes* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] auto crtc_ids() const noexcept -> std::span<u32> {
        return {handle->crtcs, static_cast<usize>(handle->count_crtcs)};
    }

    [[nodiscard]] auto connector_ids() const noexcept -> std::span<u32> {
        return {handle->connectors, static_cast<usize>(handle->count_connectors)};
    }

    [[nodiscard]] auto framebuffer_ids() const noexcept -> std::span<u32> {
        return {handle->fbs, static_cast<usize>(handle->count_fbs)};
    }

    [[nodiscard]] auto encoder_ids() const noexcept -> std::span<u32> {
        return {handle->encoders, static_cast<usize>(handle->count_encoders)};
    }

    [[nodiscard]] static auto wrap(drmModeRes* resources) noexcept -> device_resources {
        return {resources};
    }

    auto destroy() const noexcept -> void {
        drmModeFreeResources(handle);
    }
};

struct connector {
    drmModeConnector* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] auto id() const noexcept -> u32 {
        return handle->connector_id;
    }

    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return handle->connection == DRM_MODE_CONNECTED;
    }

    [[nodiscard]] auto mode_count() const noexcept -> u32 {
        return static_cast<u32>(handle->count_modes);
    }

    [[nodiscard]] auto mode(u32 index) const noexcept -> display_mode {
        return display_mode{handle->modes[index]};
    }

    [[nodiscard]] auto property_ids() const noexcept -> std::span<u32> {
        return {handle->props, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] auto encoder_ids() const noexcept -> std::span<u32> {
        return {handle->encoders, static_cast<usize>(handle->count_encoders)};
    }

    [[nodiscard]] static auto wrap(drmModeConnector* value) noexcept -> connector {
        return {value};
    }

    auto destroy() const noexcept -> void {
        drmModeFreeConnector(handle);
    }
};

struct crtc {
    drmModeCrtc* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] static auto wrap(drmModeCrtc* value) noexcept -> crtc {
        return {value};
    }

    auto destroy() const noexcept -> void {
        drmModeFreeCrtc(handle);
    }
};

struct property_set {
    drmModeObjectProperties* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] auto property_ids() const noexcept -> std::span<u32> {
        return {handle->props, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] auto property_values() const noexcept -> std::span<u64> {
        return {handle->prop_values, static_cast<usize>(handle->count_props)};
    }

    [[nodiscard]] static auto wrap(drmModeObjectProperties* properties) noexcept -> property_set {
        return {properties};
    }

    auto destroy() const noexcept -> void {
        drmModeFreeObjectProperties(handle);
    }
};

struct atomic_request {
    drmModeAtomicReq* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
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

    [[nodiscard]] auto add_property(u32 object_id, u32 property_id, u64 value) const noexcept
        -> i32 {
        const i32 result =
            static_cast<i32>(drmModeAtomicAddProperty(handle, object_id, property_id, value));
        return result < 0 ? result : 0;
    }

    [[nodiscard]] auto merge(const atomic_request& other) const noexcept -> bool {
        return drmModeAtomicMerge(handle, other.handle) == 0;
    }

    [[nodiscard]] auto duplicate() const noexcept -> atomic_request {
        return {drmModeAtomicDuplicate(handle)};
    }

    auto destroy() const noexcept -> void {
        drmModeAtomicFree(handle);
    }
};

struct property_info {
    drmModePropertyRes* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view {
        return handle->name;
    }

    [[nodiscard]] static auto wrap(drmModePropertyRes* property) noexcept -> property_info {
        return {property};
    }

    auto destroy() const noexcept -> void {
        drmModeFreeProperty(handle);
    }
};

struct plane_set {
    drmModePlaneRes* handle = nullptr;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle != nullptr;
    }

    [[nodiscard]] auto plane_ids() const noexcept -> std::span<const u32> {
        return {handle->planes, static_cast<usize>(handle->count_planes)};
    }

    [[nodiscard]] static auto wrap(drmModePlaneRes* planes) noexcept -> plane_set {
        return {planes};
    }

    auto destroy() const noexcept -> void {
        drmModeFreePlaneResources(handle);
    }
};

struct output_target {
    u32 crtc_id = 0;
    u32 connector_id = 0;
    u32 plane_id = 0;
    display_mode mode{};
};

struct mapped_memory {
    void* address = MAP_FAILED;
    usize size_bytes = 0;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return address != MAP_FAILED;
    }

    auto destroy() const noexcept -> void {
        ::munmap(address, size_bytes);
    }
};

class dumb_buffer {
  public:
    dumb_buffer() noexcept = default;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return handle_ != 0;
    }

    [[nodiscard]] auto handle() const noexcept -> u32 {
        return handle_;
    }

    [[nodiscard]] auto format() const noexcept -> const buffer_format& {
        return format_;
    }

    [[nodiscard]] auto map() const noexcept -> result<mapped_memory>;

    auto destroy() const noexcept -> void;

  private:
    friend class device;

    dumb_buffer(handle_t device_fd, u32 handle, buffer_format format) noexcept
        : device_fd_(device_fd), handle_(handle), format_(format) {
    }

    handle_t device_fd_ = -1;
    u32 handle_ = 0;
    buffer_format format_{};
};

class property_blob {
  public:
    property_blob() noexcept = default;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return id_ != 0;
    }

    [[nodiscard]] auto id() const noexcept -> u32 {
        return id_;
    }

    auto destroy() const noexcept -> void;

  private:
    friend class device;

    property_blob(handle_t device_fd, u32 id) noexcept : device_fd_(device_fd), id_(id) {
    }

    handle_t device_fd_ = -1;
    u32 id_ = 0;
};

class framebuffer {
  public:
    framebuffer() noexcept = default;

    [[nodiscard]] auto valid() const noexcept -> bool {
        return id_ != 0;
    }

    [[nodiscard]] auto id() const noexcept -> u32 {
        return id_;
    }

    auto destroy() const noexcept -> void;

  private:
    friend class device;

    framebuffer(handle_t device_fd, u32 id) noexcept : device_fd_(device_fd), id_(id) {
    }

    handle_t device_fd_ = -1;
    u32 id_ = 0;
};

class device {
  public:
    device() noexcept = default;

    explicit device(handle_t fd, bool owns_fd = true) noexcept : fd_handle_(fd), owns_fd_(owns_fd) {
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return fd_handle_ >= 0;
    }

    [[nodiscard]] auto fd() const noexcept -> handle_t {
        return fd_handle_;
    }

    [[nodiscard]] static auto create(const fs::path& path) noexcept -> result<device>;

    /// Wraps an existing fd without taking ownership. The caller must keep it open.
    [[nodiscard]] static auto create_borrowed(handle_t fd) noexcept -> device;

    [[nodiscard]] static auto open_first_available() noexcept -> result<device>;

    [[nodiscard]] auto is_master() const noexcept -> bool;

    [[nodiscard]] auto set_master() const noexcept -> result<void>;

    [[nodiscard]] auto drop_master() const noexcept -> result<void>;

    [[nodiscard]] auto set_universal_planes_enabled(bool enabled) const noexcept -> result<void>;

    [[nodiscard]] auto set_atomic_enabled(bool enabled) const noexcept -> result<void>;

    [[nodiscard]] auto get_resources() const noexcept -> device_resources;

    [[nodiscard]] auto get_plane_resources() const noexcept -> plane_set;

    [[nodiscard]] auto get_property_info(u32 property_id) const noexcept -> property_info;

    [[nodiscard]] auto get_connector_properties(u32 connector_id) const noexcept -> property_set;

    [[nodiscard]] auto get_crtc_properties(u32 crtc_id) const noexcept -> property_set;

    [[nodiscard]] auto get_plane_properties(u32 plane_id) const noexcept -> property_set;

    [[nodiscard]] auto commit_atomic(const atomic_request& request, u32 flags) const noexcept
        -> result<void>;

    [[nodiscard]] auto get_crtc(u32 crtc_id) const noexcept -> crtc;

    [[nodiscard]] auto get_connector(u32 connector_id) const noexcept -> connector;

    [[nodiscard]] auto find_primary_output() const noexcept -> result<output_target>;

    [[nodiscard]] auto wait_for_page_flip(i32 timeout_ms) const noexcept -> result<void>;

    using page_flip_callback = auto (*)(void* context) -> void;

    [[nodiscard]] auto handle_events(page_flip_callback callback, void* context) const noexcept
        -> result<void>;

    /// Creates a dumb buffer owned by this device. The device must outlive the buffer.
    [[nodiscard]] auto create_dumb_buffer(const buffer_format& format) const noexcept
        -> result<dumb_buffer>;

    /// Creates a framebuffer owned by this device. The device must outlive the framebuffer.
    [[nodiscard]] auto create_framebuffer(const dumb_buffer& buffer) const noexcept
        -> result<framebuffer>;

    /// Creates a property blob owned by this device. The device must outlive the blob.
    [[nodiscard]] auto create_property_blob(std::span<const std::byte> data) const noexcept
        -> result<property_blob>;

    auto destroy() const noexcept -> void;

  private:
    mutable handle_t fd_handle_ = -1;
    mutable bool owns_fd_ = true;
};

}