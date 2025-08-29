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

#include "config.h"

#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"

#include "vsrc_gfxcapture.h"

#define OFFSET(x) offsetof(GfxCaptureContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
static const AVOption gfxcapture_options[] = {
    { "window_title",    "ECMAScript regular expression to match against the window title. "
                         "Supports PCRE style (?i) prefix for case-insensitivity.",
                                                              OFFSET(window_text),    AV_OPT_TYPE_STRING,     { .str = NULL },   0, INT_MAX,    FLAGS },
    { "window_class",    "as window_title, but against the window class",
                                                              OFFSET(window_class),   AV_OPT_TYPE_STRING,     { .str = NULL },   0, INT_MAX,    FLAGS },
    { "window_exe",      "as window_title, but against the windows executable name",
                                                              OFFSET(window_exe),     AV_OPT_TYPE_STRING,     { .str = NULL },   0, INT_MAX,    FLAGS },
    { "monitor_idx",     "index of the monitor to capture",   OFFSET(monitor_idx),    AV_OPT_TYPE_INT,        { .i64 = GFX_MONITOR_IDX_DEFAULT }, GFX_MONITOR_IDX_DEFAULT, INT_MAX, FLAGS, .unit = "monitor_idx" },
    { "window",          "derive from selected window",       0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_MONITOR_IDX_WINDOW }, 0, 0, FLAGS, .unit = "monitor_idx" },
    { "capture_cursor",  "capture mouse cursor",              OFFSET(capture_cursor), AV_OPT_TYPE_BOOL,       { .i64 = 1 },      0, 1,          FLAGS },
    { "capture_border",  "capture full window border",        OFFSET(capture_border), AV_OPT_TYPE_BOOL,       { .i64 = 0 },      0, 1,          FLAGS },
    { "display_border",  "display yellow border around captured window",
                                                              OFFSET(display_border), AV_OPT_TYPE_BOOL,       { .i64 = 0 },      0, 1,          FLAGS },
    { "max_framerate",   "set maximum capture frame rate",    OFFSET(frame_rate),     AV_OPT_TYPE_VIDEO_RATE, { .str = "60" },   0.001, 1000,   FLAGS },
    { "hwnd",            "pre-existing HWND handle",          OFFSET(user_hwnd),      AV_OPT_TYPE_UINT64,     { .i64 = 0 },      0, UINT64_MAX, FLAGS },
    { "hmonitor",        "pre-existing HMONITOR handle",      OFFSET(user_hmonitor),  AV_OPT_TYPE_UINT64,     { .i64 = 0 },      0, UINT64_MAX, FLAGS },
    { "width",           "force width of the output frames, negative values round down the width to the nearest multiple of that number",
                                                              OFFSET(canvas_width),   AV_OPT_TYPE_INT,        { .i64 = 0 },      INT_MIN, INT_MAX, FLAGS },
    { "height",          "force height of the output frames, negative values round down the height to the nearest multiple of that number",
                                                              OFFSET(canvas_height),  AV_OPT_TYPE_INT,        { .i64 = 0 },      INT_MIN, INT_MAX, FLAGS },
    { "crop_left",       "number of pixels to crop from the left of the captured area",
                                                              OFFSET(crop_left),      AV_OPT_TYPE_INT,        { .i64 = 0 },      0, INT_MAX,    FLAGS },
    { "crop_top",        "number of pixels to crop from the top of the captured area",
                                                              OFFSET(crop_top),       AV_OPT_TYPE_INT,        { .i64 = 0 },      0, INT_MAX,    FLAGS },
    { "crop_right",      "number of pixels to crop from the right of the captured area",
                                                              OFFSET(crop_right),     AV_OPT_TYPE_INT,        { .i64 = 0 },      0, INT_MAX,    FLAGS },
    { "crop_bottom",     "number of pixels to crop from the bottom of the captured area",
                                                              OFFSET(crop_bottom),    AV_OPT_TYPE_INT,        { .i64 = 0 },      0, INT_MAX,    FLAGS },
    { "premultiplied",   "return premultiplied alpha frames", OFFSET(premult_alpha),  AV_OPT_TYPE_BOOL,       { .i64 = 0 },      0, 1,          FLAGS },
    { "resize_mode",     "capture source resize behavior",    OFFSET(resize_mode),    AV_OPT_TYPE_INT,        { .i64 = GFX_RESIZE_CROP },  0, GFX_RESIZE_NB - 1, FLAGS, .unit = "resize_mode" },
    { "crop",            "crop or add black bars into frame", 0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_RESIZE_CROP  }, 0, 0, FLAGS, .unit = "resize_mode" },
    { "scale",           "scale source to fit initial size",  0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_RESIZE_SCALE }, 0, 0, FLAGS, .unit = "resize_mode" },
    { "scale_aspect",    "scale source to fit initial size while preserving aspect ratio",
                                                              0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_RESIZE_SCALE_ASPECT }, 0, 0, FLAGS, .unit = "resize_mode" },
    { "scale_mode",      "scaling algorithm",                 OFFSET(scale_mode),     AV_OPT_TYPE_INT,        { .i64 = GFX_SCALE_BILINEAR },      0, GFX_SCALE_NB - 1, FLAGS, .unit = "scale_mode" },
    { "point",           "use point scaling",                 0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_SCALE_POINT },         0, 0, FLAGS, .unit = "scale_mode" },
    { "bilinear",        "use bilinear scaling",              0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_SCALE_BILINEAR },      0, 0, FLAGS, .unit = "scale_mode" },
    { "bicubic",         "use bicubic scaling",               0,                      AV_OPT_TYPE_CONST,      { .i64 = GFX_SCALE_BICUBIC },       0, 0, FLAGS, .unit = "scale_mode" },
    { "output_fmt",      "desired output format",             OFFSET(out_fmt),        AV_OPT_TYPE_INT,        { .i64 = AV_PIX_FMT_BGRA },   0, INT_MAX, FLAGS, .unit = "output_fmt" },
    { "8bit",            "output default 8 Bit format",       0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_BGRA },         0, 0, FLAGS, .unit = "output_fmt" },
    { "bgra",            "output 8 Bit BGRA",                 0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_BGRA },         0, 0, FLAGS, .unit = "output_fmt" },
    { "10bit",           "output default 10 Bit format",      0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_X2BGR10 },      0, 0, FLAGS, .unit = "output_fmt" },
    { "x2bgr10",         "output 10 Bit X2BGR10",             0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_X2BGR10 },      0, 0, FLAGS, .unit = "output_fmt" },
    { "16bit",           "output default 16 Bit format",      0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_RGBAF16 },      0, 0, FLAGS, .unit = "output_fmt" },
    { "rgbaf16",         "output 16 Bit RGBAF16",             0,                      AV_OPT_TYPE_CONST,      { .i64 = AV_PIX_FMT_RGBAF16 },      0, 0, FLAGS, .unit = "output_fmt" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(gfxcapture);

static const AVFilterPad gfxcapture_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = ff_gfxcapture_config_props,
    },
};

const FFFilter ff_vsrc_gfxcapture = {
    .p.name        = "gfxcapture",
    .p.description = NULL_IF_CONFIG_SMALL("Capture graphics/screen content as a video source"),
    .p.priv_class  = &gfxcapture_class,
    .p.inputs      = NULL,
    .p.flags       = AVFILTER_FLAG_HWDEVICE,
    .priv_size     = sizeof(GfxCaptureContext),
    .init          = ff_gfxcapture_init,
    .uninit        = ff_gfxcapture_uninit,
    FILTER_OUTPUTS(gfxcapture_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_D3D11),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
    .activate      = ff_gfxcapture_activate,
};
