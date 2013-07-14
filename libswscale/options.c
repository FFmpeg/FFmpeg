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

#include "libavutil/avutil.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "swscale.h"
#include "swscale_internal.h"

static const char *sws_context_to_name(void *ptr)
{
    return "swscaler";
}

#define OFFSET(x) offsetof(SwsContext, x)
#define DEFAULT 0
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM

static const AVOption options[] = {
    { "sws_flags",       "scaler flags",                  OFFSET(flags),     AV_OPT_TYPE_FLAGS,  { .i64 = DEFAULT            }, 0,       UINT_MAX,       VE, "sws_flags" },
    { "fast_bilinear",   "fast bilinear",                 0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_FAST_BILINEAR  }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "bilinear",        "bilinear",                      0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_BILINEAR       }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "bicubic",         "bicubic",                       0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_BICUBIC        }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "experimental",    "experimental",                  0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_X              }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "neighbor",        "nearest neighbor",              0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_POINT          }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "area",            "averaging area",                0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_AREA           }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "bicublin",        "luma bicubic, chroma bilinear", 0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_BICUBLIN       }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "gauss",           "gaussian",                      0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_GAUSS          }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "sinc",            "sinc",                          0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_SINC           }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "lanczos",         "lanczos",                       0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_LANCZOS        }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "spline",          "natural bicubic spline",        0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_SPLINE         }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "print_info",      "print info",                    0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_PRINT_INFO     }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "accurate_rnd",    "accurate rounding",             0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_ACCURATE_RND   }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "full_chroma_int", "full chroma interpolation",     0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_FULL_CHR_H_INT }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "full_chroma_inp", "full chroma input",             0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_FULL_CHR_H_INP }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "bitexact",        "",                              0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_BITEXACT       }, INT_MIN, INT_MAX,        VE, "sws_flags" },
    { "error_diffusion", "error diffusion dither",        0,                 AV_OPT_TYPE_CONST,  { .i64  = SWS_ERROR_DIFFUSION}, INT_MIN, INT_MAX,        VE, "sws_flags" },

    { "srcw",            "source width",                  OFFSET(srcW),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX,        VE },
    { "srch",            "source height",                 OFFSET(srcH),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX,        VE },
    { "dstw",            "destination width",             OFFSET(dstW),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX,        VE },
    { "dsth",            "destination height",            OFFSET(dstH),      AV_OPT_TYPE_INT,    { .i64 = 16                 }, 1,       INT_MAX,        VE },
    { "src_format",      "source format",                 OFFSET(srcFormat), AV_OPT_TYPE_INT,    { .i64 = DEFAULT            }, 0,       AV_PIX_FMT_NB - 1, VE },
    { "dst_format",      "destination format",            OFFSET(dstFormat), AV_OPT_TYPE_INT,    { .i64 = DEFAULT            }, 0,       AV_PIX_FMT_NB - 1, VE },
    { "src_range",       "source range",                  OFFSET(srcRange),  AV_OPT_TYPE_INT,    { .i64 = DEFAULT            }, 0,       1,              VE },
    { "dst_range",       "destination range",             OFFSET(dstRange),  AV_OPT_TYPE_INT,    { .i64 = DEFAULT            }, 0,       1,              VE },
    { "param0",          "scaler param 0",                OFFSET(param[0]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX,        VE },
    { "param1",          "scaler param 1",                OFFSET(param[1]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX,        VE },

    { "src_v_chr_pos",   "source vertical chroma position in luma grid/256"  , OFFSET(src_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -1            }, -1,      512,             VE },
    { "src_h_chr_pos",   "source horizontal chroma position in luma grid/256", OFFSET(src_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -1            }, -1,      512,             VE },
    { "dst_v_chr_pos",   "source vertical chroma position in luma grid/256"  , OFFSET(dst_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -1            }, -1,      512,             VE },
    { "dst_h_chr_pos",   "source horizontal chroma position in luma grid/256", OFFSET(dst_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -1            }, -1,      512,             VE },

    { NULL }
};

const AVClass sws_context_class = {
    .class_name = "SWScaler",
    .item_name  = sws_context_to_name,
    .option     = options,
    .category   = AV_CLASS_CATEGORY_SWSCALER,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVClass *sws_get_class(void)
{
    return &sws_context_class;
}
