// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/types.hpp>

#include <drm_fourcc.h>

namespace pwm::platform::drm {

inline constexpr u32 format_xrgb8888 = DRM_FORMAT_XRGB8888;
inline constexpr u32 format_argb8888 = DRM_FORMAT_ARGB8888;
inline constexpr u32 format_abgr8888 = DRM_FORMAT_ABGR8888;

enum class pixel_format : u32 {
    xrgb8888,
    argb8888,
};

struct buffer_format {
    u32 width_px;
    u32 height_px;
    u32 pitch_bytes;
    u8 depth_bits;
    u8 bpp;
    u64 size_bytes;
};

}