// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <base/types.hpp>

#include <meta>

namespace pwm::platform::drm::prop::id {

struct crtc {
    [[= std::define_static_string("ACTIVE")]] u32 active;
    [[= std::define_static_string("MODE_ID")]] u32 mode_id;
    [[= std::define_static_string("OUT_FENCE_PTR")]] u32 out_fence_ptr;
    [[= std::define_static_string("VRR_ENABLE")]] u32 vrr_enable;
    [[= std::define_static_string("SCALING_FILTER")]] u32 scaling_filter;
    [[= std::define_static_string("BACKGROUND_COLOR")]] u32 background_color;
    [[= std::define_static_string("DEGAMMA_LUT")]] u32 degamma_lut;
    [[= std::define_static_string("DEGAMMA_LUT_SIZE")]] u32 degamma_lut_size;
    [[= std::define_static_string("CTM")]] u32 ctm;
    [[= std::define_static_string("GAMMA_LUT")]] u32 gamma_lut;
    [[= std::define_static_string("GAMMA_LUT_SIZE")]] u32 gamma_lut_size;
};

struct connector {
    [[= std::define_static_string("EDID")]] u32 edid;
    [[= std::define_static_string("DPMS")]] u32 dpms;
    [[= std::define_static_string("link-status")]] u32 link_status;
    [[= std::define_static_string("non-desktop")]] u32 non_desktop;
    [[= std::define_static_string("TILE")]] u32 tile;
    [[= std::define_static_string("CRTC_ID")]] u32 crtc_id;
    [[= std::define_static_string("scaling mode")]] u32 scaling_mode;
    [[= std::define_static_string("panel orientation")]] u32 panel_orientation;
    [[= std::define_static_string("Broadcast RGB")]] u32 broadcast_rgb;
    [[= std::define_static_string("max bpc")]] u32 max_bpc;
    [[= std::define_static_string("Colorspace")]] u32 colorspace;
    [[= std::define_static_string("HDR_OUTPUT_METADATA")]] u32 hdr_output_metadata;
    [[= std::define_static_string("vrr_capable")]] u32 vrr_capable;
};

struct plane {
    [[= std::define_static_string("CRTC_ID")]] u32 crtc_id;
    [[= std::define_static_string("FB_ID")]] u32 fb_id;
    [[= std::define_static_string("CRTC_X")]] u32 crtc_x;
    [[= std::define_static_string("CRTC_Y")]] u32 crtc_y;
    [[= std::define_static_string("CRTC_W")]] u32 crtc_w;
    [[= std::define_static_string("CRTC_H")]] u32 crtc_h;
    [[= std::define_static_string("SRC_X")]] u32 src_x;
    [[= std::define_static_string("SRC_Y")]] u32 src_y;
    [[= std::define_static_string("SRC_W")]] u32 src_w;
    [[= std::define_static_string("SRC_H")]] u32 src_h;
};

}