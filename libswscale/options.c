/*
 * Copyright (C) 2001-2003 Michael Niedermayer <michaelni@gmx.at>
 *
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

#include "libavutil/opt.h"
#include "swscale.h"
#include "swscale_internal.h"

static const char *sws_context_to_name(void *ptr)
{
    return "swscaler";
}

#define OFFSET(x) offsetof(SwsInternal, x)
#define DEFAULT 0
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption swscale_options[] = {
    { "sws_flags",           "swscale flags",     OFFSET(flags),  AV_OPT_TYPE_FLAGS, { .i64 = SWS_BICUBIC        }, .flags = VE, .unit = "sws_flags", .max = UINT_MAX },
        { "fast_bilinear",   "fast bilinear",                 0,  AV_OPT_TYPE_CONST, { .i64 = SWS_FAST_BILINEAR  }, .flags = VE, .unit = "sws_flags" },
        { "bilinear",        "bilinear",                      0,  AV_OPT_TYPE_CONST, { .i64 = SWS_BILINEAR       }, .flags = VE, .unit = "sws_flags" },
        { "bicubic",         "bicubic",                       0,  AV_OPT_TYPE_CONST, { .i64 = SWS_BICUBIC        }, .flags = VE, .unit = "sws_flags" },
        { "experimental",    "experimental",                  0,  AV_OPT_TYPE_CONST, { .i64 = SWS_X              }, .flags = VE, .unit = "sws_flags" },
        { "neighbor",        "nearest neighbor",              0,  AV_OPT_TYPE_CONST, { .i64 = SWS_POINT          }, .flags = VE, .unit = "sws_flags" },
        { "area",            "averaging area",                0,  AV_OPT_TYPE_CONST, { .i64 = SWS_AREA           }, .flags = VE, .unit = "sws_flags" },
        { "bicublin",        "luma bicubic, chroma bilinear", 0,  AV_OPT_TYPE_CONST, { .i64 = SWS_BICUBLIN       }, .flags = VE, .unit = "sws_flags" },
        { "gauss",           "gaussian approximation",        0,  AV_OPT_TYPE_CONST, { .i64 = SWS_GAUSS          }, .flags = VE, .unit = "sws_flags" },
        { "sinc",            "sinc",                          0,  AV_OPT_TYPE_CONST, { .i64 = SWS_SINC           }, .flags = VE, .unit = "sws_flags" },
        { "lanczos",         "lanczos (sinc/sinc)",           0,  AV_OPT_TYPE_CONST, { .i64 = SWS_LANCZOS        }, .flags = VE, .unit = "sws_flags" },
        { "spline",          "natural bicubic spline",        0,  AV_OPT_TYPE_CONST, { .i64 = SWS_SPLINE         }, .flags = VE, .unit = "sws_flags" },
        { "print_info",      "print info",                    0,  AV_OPT_TYPE_CONST, { .i64 = SWS_PRINT_INFO     }, .flags = VE, .unit = "sws_flags" },
        { "accurate_rnd",    "accurate rounding",             0,  AV_OPT_TYPE_CONST, { .i64 = SWS_ACCURATE_RND   }, .flags = VE, .unit = "sws_flags" },
        { "full_chroma_int", "full chroma interpolation",     0,  AV_OPT_TYPE_CONST, { .i64 = SWS_FULL_CHR_H_INT }, .flags = VE, .unit = "sws_flags" },
        { "full_chroma_inp", "full chroma input",             0,  AV_OPT_TYPE_CONST, { .i64 = SWS_FULL_CHR_H_INP }, .flags = VE, .unit = "sws_flags" },
        { "bitexact",        "bit-exact mode",                0,  AV_OPT_TYPE_CONST, { .i64 = SWS_BITEXACT       }, .flags = VE, .unit = "sws_flags" },
        { "error_diffusion", "error diffusion dither",        0,  AV_OPT_TYPE_CONST, { .i64 = SWS_ERROR_DIFFUSION}, .flags = VE, .unit = "sws_flags" },

    { "param0",              "scaler param 0", OFFSET(param[0]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX, VE },
    { "param1",              "scaler param 1", OFFSET(param[1]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX, VE },

    { "srcw",            "source width",                  OFFSET(srcW),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX, VE },
    { "srch",            "source height",                 OFFSET(srcH),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX, VE },
    { "dstw",            "destination width",             OFFSET(dstW),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX, VE },
    { "dsth",            "destination height",            OFFSET(dstH),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX, VE },
    { "src_format",      "source format",                 OFFSET(srcFormat), AV_OPT_TYPE_PIXEL_FMT,{ .i64 = DEFAULT          }, 0,       INT_MAX, VE },
    { "dst_format",      "destination format",            OFFSET(dstFormat), AV_OPT_TYPE_PIXEL_FMT,{ .i64 = DEFAULT          }, 0,       INT_MAX, VE },
    { "src_range",       "source is full range",          OFFSET(srcRange),  AV_OPT_TYPE_BOOL,   { .i64 = DEFAULT            }, 0,       1,       VE },
    { "dst_range",       "destination is full range",     OFFSET(dstRange),  AV_OPT_TYPE_BOOL,   { .i64 = DEFAULT            }, 0,       1,       VE },
    { "gamma",           "gamma correct scaling",         OFFSET(gamma_flag),AV_OPT_TYPE_BOOL,   { .i64  = 0                 }, 0,       1,       VE },

    { "src_v_chr_pos",   "source vertical chroma position in luma grid/256"  ,      OFFSET(src_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513 }, -513, 1024, VE },
    { "src_h_chr_pos",   "source horizontal chroma position in luma grid/256",      OFFSET(src_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513 }, -513, 1024, VE },
    { "dst_v_chr_pos",   "destination vertical chroma position in luma grid/256"  , OFFSET(dst_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513 }, -513, 1024, VE },
    { "dst_h_chr_pos",   "destination horizontal chroma position in luma grid/256", OFFSET(dst_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513 }, -513, 1024, VE },

    { "sws_dither",      "set dithering algorithm",       OFFSET(dither),    AV_OPT_TYPE_INT,    { .i64  = SWS_DITHER_AUTO     }, .flags = VE, .unit = "sws_dither", .max = SWS_DITHER_NB - 1 },
        { "auto",        "automatic selection",           0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_DITHER_AUTO     }, .flags = VE, .unit = "sws_dither" },
        { "bayer",       "ordered matrix dither",         0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_DITHER_BAYER    }, .flags = VE, .unit = "sws_dither" },
        { "ed",          "full error diffusion",          0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_DITHER_ED       }, .flags = VE, .unit = "sws_dither" },
        { "a_dither",    "arithmetic addition dither",    0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_DITHER_A_DITHER }, .flags = VE, .unit = "sws_dither" },
        { "x_dither",    "arithmetic xor dither",         0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_DITHER_X_DITHER }, .flags = VE, .unit = "sws_dither" },

    { "alphablend",          "mode for alpha -> non alpha",   OFFSET(alphablend),  AV_OPT_TYPE_INT,    { .i64  = SWS_ALPHA_BLEND_NONE},         .flags = VE, .unit = "alphablend", .max = SWS_ALPHA_BLEND_NB - 1 },
        { "none",            "ignore alpha",                  0,                   AV_OPT_TYPE_CONST,  { .i64  = SWS_ALPHA_BLEND_NONE},         .flags = VE, .unit = "alphablend" },
        { "uniform_color",   "blend onto a uniform color",    0,                   AV_OPT_TYPE_CONST,  { .i64  = SWS_ALPHA_BLEND_UNIFORM},      .flags = VE, .unit = "alphablend" },
        { "checkerboard",    "blend onto a checkerboard",     0,                   AV_OPT_TYPE_CONST,  { .i64  = SWS_ALPHA_BLEND_CHECKERBOARD}, .flags = VE, .unit = "alphablend" },

    { "threads",         "number of threads",             OFFSET(nb_threads),   AV_OPT_TYPE_INT, {.i64 = 1 }, 0, INT_MAX, VE, .unit = "threads" },
        { "auto",        "automatic selection",           0,                  AV_OPT_TYPE_CONST, {.i64 = 0 },    .flags = VE, .unit = "threads" },

    { NULL }
};

const AVClass ff_sws_context_class = {
    .class_name = "SWScaler",
    .item_name  = sws_context_to_name,
    .option     = swscale_options,
    .parent_log_context_offset = OFFSET(parent),
    .category   = AV_CLASS_CATEGORY_SWSCALER,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVClass *sws_get_class(void)
{
    return &ff_sws_context_class;
}
