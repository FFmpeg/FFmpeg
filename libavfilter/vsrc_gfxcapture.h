/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_VSRC_GFXCAPTURE_H
#define AVFILTER_VSRC_GFXCAPTURE_H

#include "libavutil/log.h"
#include "libavutil/rational.h"
#include "libavfilter/avfilter.h"

typedef struct GfxCaptureContextCpp GfxCaptureContextCpp;

enum GfxResizeMode {
    GFX_RESIZE_CROP = 0,
    GFX_RESIZE_SCALE,
    GFX_RESIZE_SCALE_ASPECT,
    GFX_RESIZE_NB
};

enum GfxScaleMode {
    GFX_SCALE_POINT = 0,
    GFX_SCALE_BILINEAR,
    GFX_SCALE_BICUBIC,
    GFX_SCALE_NB
};

enum GfxMonitorIdx {
    GFX_MONITOR_IDX_WINDOW = -1,
    GFX_MONITOR_IDX_DEFAULT = -2
};

typedef struct GfxCaptureContext {
    const AVClass *avclass;

    GfxCaptureContextCpp *ctx;

    const char *window_text;
    const char *window_class;
    const char *window_exe;
    int monitor_idx;

    uint64_t user_hwnd;
    uint64_t user_hmonitor;

    int capture_cursor;
    int capture_border;
    int display_border;
    AVRational frame_rate;
    int canvas_width, canvas_height;
    int crop_left, crop_top, crop_right, crop_bottom;
    int out_fmt;
    int resize_mode;
    int scale_mode;
    int premult_alpha;
} GfxCaptureContext;

#ifdef __cplusplus
#define NOEXCEPT noexcept
#else
#define NOEXCEPT
#endif

av_cold int ff_gfxcapture_init(AVFilterContext *avctx) NOEXCEPT;
av_cold void ff_gfxcapture_uninit(AVFilterContext *avctx) NOEXCEPT;
int ff_gfxcapture_activate(AVFilterContext *avctx) NOEXCEPT;
int ff_gfxcapture_config_props(AVFilterLink *outlink) NOEXCEPT;

#undef NOEXCEPT

#endif /* AVFILTER_VSRC_GFXCAPTURE_H */
