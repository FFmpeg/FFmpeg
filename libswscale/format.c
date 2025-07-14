/*
 * Copyright (C) 2024 Niklas Haas
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

#include "libavutil/avassert.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/refstruct.h"

#include "format.h"
#include "csputils.h"
#include "ops_internal.h"

#define Q(N) ((AVRational) { N, 1 })
#define Q0   Q(0)
#define Q1   Q(1)

#define RET(x)                                                                 \
    do {                                                                       \
        int _ret = (x);                                                        \
        if (_ret  < 0)                                                         \
            return _ret;                                                       \
    } while (0)

typedef struct LegacyFormatEntry {
    uint8_t is_supported_in         :1;
    uint8_t is_supported_out        :1;
    uint8_t is_supported_endianness :1;
} LegacyFormatEntry;

/* Format support table for legacy swscale */
static const LegacyFormatEntry legacy_format_entries[] = {
    [AV_PIX_FMT_YUV420P]        = { 1, 1 },
    [AV_PIX_FMT_YUYV422]        = { 1, 1 },
    [AV_PIX_FMT_RGB24]          = { 1, 1 },
    [AV_PIX_FMT_BGR24]          = { 1, 1 },
    [AV_PIX_FMT_YUV422P]        = { 1, 1 },
    [AV_PIX_FMT_YUV444P]        = { 1, 1 },
    [AV_PIX_FMT_YUV410P]        = { 1, 1 },
    [AV_PIX_FMT_YUV411P]        = { 1, 1 },
    [AV_PIX_FMT_GRAY8]          = { 1, 1 },
    [AV_PIX_FMT_MONOWHITE]      = { 1, 1 },
    [AV_PIX_FMT_MONOBLACK]      = { 1, 1 },
    [AV_PIX_FMT_PAL8]           = { 1, 0 },
    [AV_PIX_FMT_YUVJ420P]       = { 1, 1 },
    [AV_PIX_FMT_YUVJ411P]       = { 1, 1 },
    [AV_PIX_FMT_YUVJ422P]       = { 1, 1 },
    [AV_PIX_FMT_YUVJ444P]       = { 1, 1 },
    [AV_PIX_FMT_YVYU422]        = { 1, 1 },
    [AV_PIX_FMT_UYVY422]        = { 1, 1 },
    [AV_PIX_FMT_UYYVYY411]      = { 1, 0 },
    [AV_PIX_FMT_BGR8]           = { 1, 1 },
    [AV_PIX_FMT_BGR4]           = { 0, 1 },
    [AV_PIX_FMT_BGR4_BYTE]      = { 1, 1 },
    [AV_PIX_FMT_RGB8]           = { 1, 1 },
    [AV_PIX_FMT_RGB4]           = { 0, 1 },
    [AV_PIX_FMT_RGB4_BYTE]      = { 1, 1 },
    [AV_PIX_FMT_NV12]           = { 1, 1 },
    [AV_PIX_FMT_NV21]           = { 1, 1 },
    [AV_PIX_FMT_ARGB]           = { 1, 1 },
    [AV_PIX_FMT_RGBA]           = { 1, 1 },
    [AV_PIX_FMT_ABGR]           = { 1, 1 },
    [AV_PIX_FMT_BGRA]           = { 1, 1 },
    [AV_PIX_FMT_0RGB]           = { 1, 1 },
    [AV_PIX_FMT_RGB0]           = { 1, 1 },
    [AV_PIX_FMT_0BGR]           = { 1, 1 },
    [AV_PIX_FMT_BGR0]           = { 1, 1 },
    [AV_PIX_FMT_GRAY9BE]        = { 1, 1 },
    [AV_PIX_FMT_GRAY9LE]        = { 1, 1 },
    [AV_PIX_FMT_GRAY10BE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY10LE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY12BE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY12LE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY14BE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY14LE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY16BE]       = { 1, 1 },
    [AV_PIX_FMT_GRAY16LE]       = { 1, 1 },
    [AV_PIX_FMT_YUV440P]        = { 1, 1 },
    [AV_PIX_FMT_YUVJ440P]       = { 1, 1 },
    [AV_PIX_FMT_YUV440P10LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV440P10BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV440P12LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV440P12BE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA420P]       = { 1, 1 },
    [AV_PIX_FMT_YUVA422P]       = { 1, 1 },
    [AV_PIX_FMT_YUVA444P]       = { 1, 1 },
    [AV_PIX_FMT_YUVA420P9BE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA420P9LE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA422P9BE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA422P9LE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA444P9BE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA444P9LE]    = { 1, 1 },
    [AV_PIX_FMT_YUVA420P10BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA420P10LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA422P10BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA422P10LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P10BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P10LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA420P16BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA420P16LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA422P16BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA422P16LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P16BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P16LE]   = { 1, 1 },
    [AV_PIX_FMT_RGB48BE]        = { 1, 1 },
    [AV_PIX_FMT_RGB48LE]        = { 1, 1 },
    [AV_PIX_FMT_RGBA64BE]       = { 1, 1, 1 },
    [AV_PIX_FMT_RGBA64LE]       = { 1, 1, 1 },
    [AV_PIX_FMT_RGB565BE]       = { 1, 1 },
    [AV_PIX_FMT_RGB565LE]       = { 1, 1 },
    [AV_PIX_FMT_RGB555BE]       = { 1, 1 },
    [AV_PIX_FMT_RGB555LE]       = { 1, 1 },
    [AV_PIX_FMT_BGR565BE]       = { 1, 1 },
    [AV_PIX_FMT_BGR565LE]       = { 1, 1 },
    [AV_PIX_FMT_BGR555BE]       = { 1, 1 },
    [AV_PIX_FMT_BGR555LE]       = { 1, 1 },
    [AV_PIX_FMT_YUV420P16LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P16BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P16LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P16BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P16LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P16BE]    = { 1, 1 },
    [AV_PIX_FMT_RGB444LE]       = { 1, 1 },
    [AV_PIX_FMT_RGB444BE]       = { 1, 1 },
    [AV_PIX_FMT_BGR444LE]       = { 1, 1 },
    [AV_PIX_FMT_BGR444BE]       = { 1, 1 },
    [AV_PIX_FMT_YA8]            = { 1, 1 },
    [AV_PIX_FMT_YA16BE]         = { 1, 1 },
    [AV_PIX_FMT_YA16LE]         = { 1, 1 },
    [AV_PIX_FMT_BGR48BE]        = { 1, 1 },
    [AV_PIX_FMT_BGR48LE]        = { 1, 1 },
    [AV_PIX_FMT_BGRA64BE]       = { 1, 1, 1 },
    [AV_PIX_FMT_BGRA64LE]       = { 1, 1, 1 },
    [AV_PIX_FMT_YUV420P9BE]     = { 1, 1 },
    [AV_PIX_FMT_YUV420P9LE]     = { 1, 1 },
    [AV_PIX_FMT_YUV420P10BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P10LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P12BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P12LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P14BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV420P14LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P9BE]     = { 1, 1 },
    [AV_PIX_FMT_YUV422P9LE]     = { 1, 1 },
    [AV_PIX_FMT_YUV422P10BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P10LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P12BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P12LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P14BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV422P14LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P9BE]     = { 1, 1 },
    [AV_PIX_FMT_YUV444P9LE]     = { 1, 1 },
    [AV_PIX_FMT_YUV444P10BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P10LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P12BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P12LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P14BE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P14LE]    = { 1, 1 },
    [AV_PIX_FMT_YUV444P10MSBBE] = { 1, 1 },
    [AV_PIX_FMT_YUV444P10MSBLE] = { 1, 1 },
    [AV_PIX_FMT_YUV444P12MSBBE] = { 1, 1 },
    [AV_PIX_FMT_YUV444P12MSBLE] = { 1, 1 },
    [AV_PIX_FMT_GBRP]           = { 1, 1 },
    [AV_PIX_FMT_GBRP9LE]        = { 1, 1 },
    [AV_PIX_FMT_GBRP9BE]        = { 1, 1 },
    [AV_PIX_FMT_GBRP10LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP10BE]       = { 1, 1 },
    [AV_PIX_FMT_GBRAP10LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAP10BE]      = { 1, 1 },
    [AV_PIX_FMT_GBRP10MSBLE]    = { 1, 1 },
    [AV_PIX_FMT_GBRP10MSBBE]    = { 1, 1 },
    [AV_PIX_FMT_GBRP12LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP12BE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP12MSBLE]    = { 1, 1 },
    [AV_PIX_FMT_GBRP12MSBBE]    = { 1, 1 },
    [AV_PIX_FMT_GBRAP12LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAP12BE]      = { 1, 1 },
    [AV_PIX_FMT_GBRP14LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP14BE]       = { 1, 1 },
    [AV_PIX_FMT_GBRAP14LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAP14BE]      = { 1, 1 },
    [AV_PIX_FMT_GBRP16LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP16BE]       = { 1, 1 },
    [AV_PIX_FMT_GBRPF32LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRPF32BE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAPF32LE]     = { 1, 1 },
    [AV_PIX_FMT_GBRAPF32BE]     = { 1, 1 },
    [AV_PIX_FMT_GBRPF16LE]      = { 1, 0 },
    [AV_PIX_FMT_GBRPF16BE]      = { 1, 0 },
    [AV_PIX_FMT_GBRAPF16LE]     = { 1, 0 },
    [AV_PIX_FMT_GBRAPF16BE]     = { 1, 0 },
    [AV_PIX_FMT_GBRAP]          = { 1, 1 },
    [AV_PIX_FMT_GBRAP16LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAP16BE]      = { 1, 1 },
    [AV_PIX_FMT_BAYER_BGGR8]    = { 1, 0 },
    [AV_PIX_FMT_BAYER_RGGB8]    = { 1, 0 },
    [AV_PIX_FMT_BAYER_GBRG8]    = { 1, 0 },
    [AV_PIX_FMT_BAYER_GRBG8]    = { 1, 0 },
    [AV_PIX_FMT_BAYER_BGGR16LE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_BGGR16BE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_RGGB16LE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_RGGB16BE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_GBRG16LE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_GBRG16BE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_GRBG16LE] = { 1, 0 },
    [AV_PIX_FMT_BAYER_GRBG16BE] = { 1, 0 },
    [AV_PIX_FMT_XYZ12BE]        = { 1, 1, 1 },
    [AV_PIX_FMT_XYZ12LE]        = { 1, 1, 1 },
    [AV_PIX_FMT_AYUV64LE]       = { 1, 1},
    [AV_PIX_FMT_AYUV64BE]       = { 1, 1 },
    [AV_PIX_FMT_P010LE]         = { 1, 1 },
    [AV_PIX_FMT_P010BE]         = { 1, 1 },
    [AV_PIX_FMT_P012LE]         = { 1, 1 },
    [AV_PIX_FMT_P012BE]         = { 1, 1 },
    [AV_PIX_FMT_P016LE]         = { 1, 1 },
    [AV_PIX_FMT_P016BE]         = { 1, 1 },
    [AV_PIX_FMT_GRAYF32LE]      = { 1, 1 },
    [AV_PIX_FMT_GRAYF32BE]      = { 1, 1 },
    [AV_PIX_FMT_GRAYF16LE]      = { 1, 0 },
    [AV_PIX_FMT_GRAYF16BE]      = { 1, 0 },
    [AV_PIX_FMT_YAF32LE]        = { 1, 0 },
    [AV_PIX_FMT_YAF32BE]        = { 1, 0 },
    [AV_PIX_FMT_YAF16LE]        = { 1, 0 },
    [AV_PIX_FMT_YAF16BE]        = { 1, 0 },
    [AV_PIX_FMT_YUVA422P12BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA422P12LE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P12BE]   = { 1, 1 },
    [AV_PIX_FMT_YUVA444P12LE]   = { 1, 1 },
    [AV_PIX_FMT_NV24]           = { 1, 1 },
    [AV_PIX_FMT_NV42]           = { 1, 1 },
    [AV_PIX_FMT_Y210LE]         = { 1, 1 },
    [AV_PIX_FMT_Y212LE]         = { 1, 1 },
    [AV_PIX_FMT_Y216LE]         = { 1, 1 },
    [AV_PIX_FMT_X2RGB10LE]      = { 1, 1 },
    [AV_PIX_FMT_X2BGR10LE]      = { 1, 1 },
    [AV_PIX_FMT_NV20BE]         = { 1, 1 },
    [AV_PIX_FMT_NV20LE]         = { 1, 1 },
    [AV_PIX_FMT_P210BE]         = { 1, 1 },
    [AV_PIX_FMT_P210LE]         = { 1, 1 },
    [AV_PIX_FMT_P212BE]         = { 1, 1 },
    [AV_PIX_FMT_P212LE]         = { 1, 1 },
    [AV_PIX_FMT_P410BE]         = { 1, 1 },
    [AV_PIX_FMT_P410LE]         = { 1, 1 },
    [AV_PIX_FMT_P412BE]         = { 1, 1 },
    [AV_PIX_FMT_P412LE]         = { 1, 1 },
    [AV_PIX_FMT_P216BE]         = { 1, 1 },
    [AV_PIX_FMT_P216LE]         = { 1, 1 },
    [AV_PIX_FMT_P416BE]         = { 1, 1 },
    [AV_PIX_FMT_P416LE]         = { 1, 1 },
    [AV_PIX_FMT_NV16]           = { 1, 1 },
    [AV_PIX_FMT_VUYA]           = { 1, 1 },
    [AV_PIX_FMT_VUYX]           = { 1, 1 },
    [AV_PIX_FMT_RGBAF16BE]      = { 1, 0 },
    [AV_PIX_FMT_RGBAF16LE]      = { 1, 0 },
    [AV_PIX_FMT_RGBF16BE]       = { 1, 0 },
    [AV_PIX_FMT_RGBF16LE]       = { 1, 0 },
    [AV_PIX_FMT_RGBF32BE]       = { 1, 0 },
    [AV_PIX_FMT_RGBF32LE]       = { 1, 0 },
    [AV_PIX_FMT_XV30LE]         = { 1, 1 },
    [AV_PIX_FMT_XV36LE]         = { 1, 1 },
    [AV_PIX_FMT_XV36BE]         = { 1, 1 },
    [AV_PIX_FMT_XV48LE]         = { 1, 1 },
    [AV_PIX_FMT_XV48BE]         = { 1, 1 },
    [AV_PIX_FMT_AYUV]           = { 1, 1 },
    [AV_PIX_FMT_UYVA]           = { 1, 1 },
    [AV_PIX_FMT_VYU444]         = { 1, 1 },
    [AV_PIX_FMT_V30XLE]         = { 1, 1 },
};

int sws_isSupportedInput(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(legacy_format_entries) ?
    legacy_format_entries[pix_fmt].is_supported_in : 0;
}

int sws_isSupportedOutput(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(legacy_format_entries) ?
    legacy_format_entries[pix_fmt].is_supported_out : 0;
}

int sws_isSupportedEndiannessConversion(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(legacy_format_entries) ?
    legacy_format_entries[pix_fmt].is_supported_endianness : 0;
}

/**
 * This function also sanitizes and strips the input data, removing irrelevant
 * fields for certain formats.
 */
SwsFormat ff_fmt_from_frame(const AVFrame *frame, int field)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    const AVColorPrimariesDesc *primaries;
    AVFrameSideData *sd;

    SwsFormat fmt = {
        .width  = frame->width,
        .height = frame->height,
        .format = frame->format,
        .range  = frame->color_range,
        .csp    = frame->colorspace,
        .loc    = frame->chroma_location,
        .desc   = desc,
        .color = {
            .prim = frame->color_primaries,
            .trc  = frame->color_trc,
        },
    };

    av_assert1(fmt.width > 0);
    av_assert1(fmt.height > 0);
    av_assert1(fmt.format != AV_PIX_FMT_NONE);
    av_assert0(desc);
    if (desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BAYER)) {
        /* RGB-like family */
        fmt.csp   = AVCOL_SPC_RGB;
        fmt.range = AVCOL_RANGE_JPEG;
    } else if (desc->flags & AV_PIX_FMT_FLAG_XYZ) {
        fmt.csp   = AVCOL_SPC_UNSPECIFIED;
        fmt.color = (SwsColor) {
            .prim = AVCOL_PRI_BT709, /* swscale currently hard-codes this XYZ matrix */
            .trc  = AVCOL_TRC_SMPTE428,
        };
    } else if (desc->nb_components < 3) {
        /* Grayscale formats */
        fmt.color.prim = AVCOL_PRI_UNSPECIFIED;
        fmt.csp        = AVCOL_SPC_UNSPECIFIED;
        if (desc->flags & AV_PIX_FMT_FLAG_FLOAT)
            fmt.range = AVCOL_RANGE_UNSPECIFIED;
        else
            fmt.range = AVCOL_RANGE_JPEG; // FIXME: this restriction should be lifted
    }

    switch (frame->format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ411P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
        fmt.range = AVCOL_RANGE_JPEG;
        break;
    }

    if (!desc->log2_chroma_w && !desc->log2_chroma_h)
        fmt.loc = AVCHROMA_LOC_UNSPECIFIED;

    if (frame->flags & AV_FRAME_FLAG_INTERLACED) {
        fmt.height = (fmt.height + (field == FIELD_TOP)) >> 1;
        fmt.interlaced = 1;
    }

    /* Set luminance and gamut information */
    fmt.color.min_luma = av_make_q(0, 1);
    switch (fmt.color.trc) {
    case AVCOL_TRC_SMPTE2084:
        fmt.color.max_luma = av_make_q(10000, 1); break;
    case AVCOL_TRC_ARIB_STD_B67:
        fmt.color.max_luma = av_make_q( 1000, 1); break; /* HLG reference display */
    default:
        fmt.color.max_luma = av_make_q(  203, 1); break; /* SDR reference brightness */
    }

    primaries = av_csp_primaries_desc_from_id(fmt.color.prim);
    if (primaries)
        fmt.color.gamut = primaries->prim;

    if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA))) {
        const AVMasteringDisplayMetadata *mdm = (const AVMasteringDisplayMetadata *) sd->data;
        if (mdm->has_luminance) {
            fmt.color.min_luma = mdm->min_luminance;
            fmt.color.max_luma = mdm->max_luminance;
        }

        if (mdm->has_primaries) {
            /* Ignore mastering display white point as it has no bearance on
             * the underlying content */
            fmt.color.gamut.r.x = mdm->display_primaries[0][0];
            fmt.color.gamut.r.y = mdm->display_primaries[0][1];
            fmt.color.gamut.g.x = mdm->display_primaries[1][0];
            fmt.color.gamut.g.y = mdm->display_primaries[1][1];
            fmt.color.gamut.b.x = mdm->display_primaries[2][0];
            fmt.color.gamut.b.y = mdm->display_primaries[2][1];
        }
    }

    if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DYNAMIC_HDR_PLUS))) {
        const AVDynamicHDRPlus *dhp = (const AVDynamicHDRPlus *) sd->data;
        const AVHDRPlusColorTransformParams *pars = &dhp->params[0];
        const AVRational nits = av_make_q(10000, 1);
        AVRational maxrgb = pars->maxscl[0];

        if (!dhp->num_windows || dhp->application_version > 1)
            goto skip_hdr10;

        /* Maximum of MaxSCL components */
        if (av_cmp_q(pars->maxscl[1], maxrgb) > 0)
            maxrgb = pars->maxscl[1];
        if (av_cmp_q(pars->maxscl[2], maxrgb) > 0)
            maxrgb = pars->maxscl[2];

        if (maxrgb.num > 0) {
            /* Estimate true luminance from MaxSCL */
            const AVLumaCoefficients *luma = av_csp_luma_coeffs_from_avcsp(fmt.csp);
            if (!luma)
                goto skip_hdr10;
            fmt.color.frame_peak = av_add_q(av_mul_q(luma->cr, pars->maxscl[0]),
                                   av_add_q(av_mul_q(luma->cg, pars->maxscl[1]),
                                            av_mul_q(luma->cb, pars->maxscl[2])));
            /* Scale the scene average brightness by the ratio between the
             * maximum luminance and the MaxRGB values */
            fmt.color.frame_avg = av_mul_q(pars->average_maxrgb,
                                           av_div_q(fmt.color.frame_peak, maxrgb));
        } else {
            /**
             * Calculate largest value from histogram to use as fallback for
             * clips with missing MaxSCL information. Note that this may end
             * up picking the "reserved" value at the 5% percentile, which in
             * practice appears to track the brightest pixel in the scene.
             */
            for (int i = 0; i < pars->num_distribution_maxrgb_percentiles; i++) {
                const AVRational pct = pars->distribution_maxrgb[i].percentile;
                if (av_cmp_q(pct, maxrgb) > 0)
                    maxrgb = pct;
                fmt.color.frame_peak = maxrgb;
                fmt.color.frame_avg  = pars->average_maxrgb;
            }
        }

        /* Rescale to nits */
        fmt.color.frame_peak = av_mul_q(nits, fmt.color.frame_peak);
        fmt.color.frame_avg  = av_mul_q(nits, fmt.color.frame_avg);
    }
skip_hdr10:

    /* PQ is always scaled down to absolute zero, so ignore mastering metadata */
    if (fmt.color.trc == AVCOL_TRC_SMPTE2084)
        fmt.color.min_luma = av_make_q(0, 1);

    return fmt;
}

static int infer_prim_ref(SwsColor *csp, const SwsColor *ref)
{
    if (csp->prim != AVCOL_PRI_UNSPECIFIED)
        return 0;

    /* Reuse the reference gamut only for "safe", similar primaries */
    switch (ref->prim) {
    case AVCOL_PRI_BT709:
    case AVCOL_PRI_BT470M:
    case AVCOL_PRI_BT470BG:
    case AVCOL_PRI_SMPTE170M:
    case AVCOL_PRI_SMPTE240M:
        csp->prim  = ref->prim;
        csp->gamut = ref->gamut;
        break;
    default:
        csp->prim  = AVCOL_PRI_BT709;
        csp->gamut = av_csp_primaries_desc_from_id(csp->prim)->prim;
        break;
    }

    return 1;
}

static int infer_trc_ref(SwsColor *csp, const SwsColor *ref)
{
    if (csp->trc != AVCOL_TRC_UNSPECIFIED)
        return 0;

    /* Pick a suitable SDR transfer function, to try and minimize conversions */
    switch (ref->trc) {
    case AVCOL_TRC_UNSPECIFIED:
    /* HDR curves, never default to these */
    case AVCOL_TRC_SMPTE2084:
    case AVCOL_TRC_ARIB_STD_B67:
        csp->trc = AVCOL_TRC_BT709;
        csp->min_luma = av_make_q(0, 1);
        csp->max_luma = av_make_q(203, 1);
        break;
    default:
        csp->trc = ref->trc;
        csp->min_luma = ref->min_luma;
        csp->max_luma = ref->max_luma;
        break;
    }

    return 1;
}

bool ff_infer_colors(SwsColor *src, SwsColor *dst)
{
    int incomplete = 0;

    incomplete |= infer_prim_ref(dst, src);
    incomplete |= infer_prim_ref(src, dst);
    av_assert0(src->prim != AVCOL_PRI_UNSPECIFIED);
    av_assert0(dst->prim != AVCOL_PRI_UNSPECIFIED);

    incomplete |= infer_trc_ref(dst, src);
    incomplete |= infer_trc_ref(src, dst);
    av_assert0(src->trc != AVCOL_TRC_UNSPECIFIED);
    av_assert0(dst->trc != AVCOL_TRC_UNSPECIFIED);

    return incomplete;
}

int sws_test_format(enum AVPixelFormat format, int output)
{
    return output ? sws_isSupportedOutput(format) : sws_isSupportedInput(format);
}

int sws_test_colorspace(enum AVColorSpace csp, int output)
{
    switch (csp) {
    case AVCOL_SPC_UNSPECIFIED:
    case AVCOL_SPC_RGB:
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_BT2020_NCL:
        return 1;
    default:
        return 0;
    }
}

int sws_test_primaries(enum AVColorPrimaries prim, int output)
{
    return prim > AVCOL_PRI_RESERVED0 && prim < AVCOL_PRI_NB &&
           prim != AVCOL_PRI_RESERVED;
}

int sws_test_transfer(enum AVColorTransferCharacteristic trc, int output)
{
    av_csp_eotf_function eotf = output ? av_csp_itu_eotf_inv(trc)
                                       : av_csp_itu_eotf(trc);
    return trc == AVCOL_TRC_UNSPECIFIED || eotf != NULL;
}

static int test_range(enum AVColorRange range)
{
    return (unsigned)range < AVCOL_RANGE_NB;
}

static int test_loc(enum AVChromaLocation loc)
{
    return (unsigned)loc < AVCHROMA_LOC_NB;
}

int ff_test_fmt(const SwsFormat *fmt, int output)
{
    return fmt->width > 0 && fmt->height > 0            &&
           sws_test_format    (fmt->format,     output) &&
           sws_test_colorspace(fmt->csp,        output) &&
           sws_test_primaries (fmt->color.prim, output) &&
           sws_test_transfer  (fmt->color.trc,  output) &&
           test_range         (fmt->range)              &&
           test_loc           (fmt->loc);
}

int sws_test_frame(const AVFrame *frame, int output)
{
    for (int field = 0; field < 2; field++) {
        const SwsFormat fmt = ff_fmt_from_frame(frame, field);
        if (!ff_test_fmt(&fmt, output))
            return 0;
        if (!fmt.interlaced)
            break;
    }

    return 1;
}

int sws_is_noop(const AVFrame *dst, const AVFrame *src)
{
    for (int field = 0; field < 2; field++) {
        SwsFormat dst_fmt = ff_fmt_from_frame(dst, field);
        SwsFormat src_fmt = ff_fmt_from_frame(src, field);
        if (!ff_fmt_equal(&dst_fmt, &src_fmt))
            return 0;
        if (!dst_fmt.interlaced)
            break;
    }

    return 1;
}

#if CONFIG_UNSTABLE

/* Returns the type suitable for a pixel after fully decoding/unpacking it */
static SwsPixelType fmt_pixel_type(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    const int bits = FFALIGN(desc->comp[0].depth, 8);
    if (desc->flags & AV_PIX_FMT_FLAG_FLOAT) {
        switch (bits) {
        case 32: return SWS_PIXEL_F32;
        }
    } else {
        switch (bits) {
        case  8: return SWS_PIXEL_U8;
        case 16: return SWS_PIXEL_U16;
        case 32: return SWS_PIXEL_U32;
        }
    }

    return SWS_PIXEL_NONE;
}

static SwsSwizzleOp fmt_swizzle(enum AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_AYUV64LE:
    case AV_PIX_FMT_AYUV64BE:
    case AV_PIX_FMT_AYUV:
    case AV_PIX_FMT_X2RGB10LE:
    case AV_PIX_FMT_X2RGB10BE:
        return (SwsSwizzleOp) {{ .x = 3, 0, 1, 2 }};
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_BGR4:
    case AV_PIX_FMT_BGR4_BYTE:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR565BE:
    case AV_PIX_FMT_BGR565LE:
    case AV_PIX_FMT_BGR555BE:
    case AV_PIX_FMT_BGR555LE:
    case AV_PIX_FMT_BGR444BE:
    case AV_PIX_FMT_BGR444LE:
    case AV_PIX_FMT_BGR48BE:
    case AV_PIX_FMT_BGR48LE:
    case AV_PIX_FMT_BGRA64BE:
    case AV_PIX_FMT_BGRA64LE:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_VUYA:
    case AV_PIX_FMT_VUYX:
        return (SwsSwizzleOp) {{ .x = 2, 1, 0, 3 }};
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_X2BGR10LE:
    case AV_PIX_FMT_X2BGR10BE:
        return (SwsSwizzleOp) {{ .x = 3, 2, 1, 0 }};
    case AV_PIX_FMT_YA8:
    case AV_PIX_FMT_YA16BE:
    case AV_PIX_FMT_YA16LE:
        return (SwsSwizzleOp) {{ .x = 0, 3, 1, 2 }};
    case AV_PIX_FMT_XV30BE:
    case AV_PIX_FMT_XV30LE:
        return (SwsSwizzleOp) {{ .x = 3, 2, 0, 1 }};
    case AV_PIX_FMT_VYU444:
    case AV_PIX_FMT_V30XBE:
    case AV_PIX_FMT_V30XLE:
        return (SwsSwizzleOp) {{ .x = 2, 0, 1, 3 }};
    case AV_PIX_FMT_XV36BE:
    case AV_PIX_FMT_XV36LE:
    case AV_PIX_FMT_XV48BE:
    case AV_PIX_FMT_XV48LE:
    case AV_PIX_FMT_UYVA:
        return (SwsSwizzleOp) {{ .x = 1, 0, 2, 3 }};
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRP9BE:
    case AV_PIX_FMT_GBRP9LE:
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_GBRP14BE:
    case AV_PIX_FMT_GBRP14LE:
    case AV_PIX_FMT_GBRP16BE:
    case AV_PIX_FMT_GBRP16LE:
    case AV_PIX_FMT_GBRPF16BE:
    case AV_PIX_FMT_GBRPF16LE:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRAP10LE:
    case AV_PIX_FMT_GBRAP10BE:
    case AV_PIX_FMT_GBRAP12LE:
    case AV_PIX_FMT_GBRAP12BE:
    case AV_PIX_FMT_GBRAP14LE:
    case AV_PIX_FMT_GBRAP14BE:
    case AV_PIX_FMT_GBRAP16LE:
    case AV_PIX_FMT_GBRAP16BE:
    case AV_PIX_FMT_GBRPF32BE:
    case AV_PIX_FMT_GBRPF32LE:
    case AV_PIX_FMT_GBRAPF16BE:
    case AV_PIX_FMT_GBRAPF16LE:
    case AV_PIX_FMT_GBRAPF32BE:
    case AV_PIX_FMT_GBRAPF32LE:
    case AV_PIX_FMT_GBRP10MSBBE:
    case AV_PIX_FMT_GBRP10MSBLE:
    case AV_PIX_FMT_GBRP12MSBBE:
    case AV_PIX_FMT_GBRP12MSBLE:
        return (SwsSwizzleOp) {{ .x = 1, 2, 0, 3 }};
    default:
        return (SwsSwizzleOp) {{ .x = 0, 1, 2, 3 }};
    }
}

static SwsSwizzleOp swizzle_inv(SwsSwizzleOp swiz) {
    /* Input[x] =: Output[swizzle.x] */
    unsigned out[4];
    out[swiz.x] = 0;
    out[swiz.y] = 1;
    out[swiz.z] = 2;
    out[swiz.w] = 3;
    return (SwsSwizzleOp) {{ .x = out[0], out[1], out[2], out[3] }};
}

/* Shift factor for MSB aligned formats */
static int fmt_shift(enum AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_P010BE:
    case AV_PIX_FMT_P010LE:
    case AV_PIX_FMT_P210BE:
    case AV_PIX_FMT_P210LE:
    case AV_PIX_FMT_Y210BE:
    case AV_PIX_FMT_Y210LE:
    case AV_PIX_FMT_YUV444P10MSBBE:
    case AV_PIX_FMT_YUV444P10MSBLE:
    case AV_PIX_FMT_GBRP10MSBBE:
    case AV_PIX_FMT_GBRP10MSBLE:
        return 6;
    case AV_PIX_FMT_P012BE:
    case AV_PIX_FMT_P012LE:
    case AV_PIX_FMT_P212BE:
    case AV_PIX_FMT_P212LE:
    case AV_PIX_FMT_P412BE:
    case AV_PIX_FMT_P412LE:
    case AV_PIX_FMT_XV36BE:
    case AV_PIX_FMT_XV36LE:
    case AV_PIX_FMT_XYZ12BE:
    case AV_PIX_FMT_XYZ12LE:
    case AV_PIX_FMT_YUV444P12MSBBE:
    case AV_PIX_FMT_YUV444P12MSBLE:
    case AV_PIX_FMT_GBRP12MSBBE:
    case AV_PIX_FMT_GBRP12MSBLE:
        return 4;
    }

    return 0;
}

/**
 * This initializes all absent components explicitly to zero. There is no
 * need to worry about the correct neutral value as fmt_decode() will
 * implicitly ignore and overwrite absent components in any case. This function
 * is just to ensure that we don't operate on undefined memory. In most cases,
 * it will end up getting pushed towards the output or optimized away entirely
 * by the optimization pass.
 */
static SwsConst fmt_clear(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    const bool has_chroma = desc->nb_components >= 3;
    const bool has_alpha  = desc->flags & AV_PIX_FMT_FLAG_ALPHA;

    SwsConst c = {0};
    if (!has_chroma)
        c.q4[1] = c.q4[2] = Q0;
    if (!has_alpha)
        c.q4[3] = Q0;

    return c;
}

static int fmt_read_write(enum AVPixelFormat fmt, SwsReadWriteOp *rw_op,
                          SwsPackOp *pack_op)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc)
        return AVERROR(EINVAL);

    switch (fmt) {
    case AV_PIX_FMT_NONE:
    case AV_PIX_FMT_NB:
        break;

    /* Packed bitstream formats */
    case AV_PIX_FMT_MONOWHITE:
    case AV_PIX_FMT_MONOBLACK:
        *pack_op = (SwsPackOp) {0};
        *rw_op = (SwsReadWriteOp) {
            .elems = 1,
            .frac  = 3,
        };
        return 0;
    case AV_PIX_FMT_RGB4:
    case AV_PIX_FMT_BGR4:
        *pack_op = (SwsPackOp) {{ 1, 2, 1 }};
        *rw_op = (SwsReadWriteOp) {
            .elems = 1,
            .packed = true,
            .frac  = 1,
        };
        return 0;
    /* Packed 8-bit aligned formats */
    case AV_PIX_FMT_RGB4_BYTE:
    case AV_PIX_FMT_BGR4_BYTE:
        *pack_op = (SwsPackOp) {{ 1, 2, 1 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    case AV_PIX_FMT_BGR8:
        *pack_op = (SwsPackOp) {{ 2, 3, 3 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    case AV_PIX_FMT_RGB8:
        *pack_op = (SwsPackOp) {{ 3, 3, 2 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;

    /* Packed 16-bit aligned formats */
    case AV_PIX_FMT_RGB565BE:
    case AV_PIX_FMT_RGB565LE:
    case AV_PIX_FMT_BGR565BE:
    case AV_PIX_FMT_BGR565LE:
        *pack_op = (SwsPackOp) {{ 5, 6, 5 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    case AV_PIX_FMT_RGB555BE:
    case AV_PIX_FMT_RGB555LE:
    case AV_PIX_FMT_BGR555BE:
    case AV_PIX_FMT_BGR555LE:
        *pack_op = (SwsPackOp) {{ 5, 5, 5 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    case AV_PIX_FMT_RGB444BE:
    case AV_PIX_FMT_RGB444LE:
    case AV_PIX_FMT_BGR444BE:
    case AV_PIX_FMT_BGR444LE:
        *pack_op = (SwsPackOp) {{ 4, 4, 4 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    /* Packed 32-bit aligned 4:4:4 formats */
    case AV_PIX_FMT_X2RGB10BE:
    case AV_PIX_FMT_X2RGB10LE:
    case AV_PIX_FMT_X2BGR10BE:
    case AV_PIX_FMT_X2BGR10LE:
    case AV_PIX_FMT_XV30BE:
    case AV_PIX_FMT_XV30LE:
        *pack_op = (SwsPackOp) {{ 2, 10, 10, 10 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    case AV_PIX_FMT_V30XBE:
    case AV_PIX_FMT_V30XLE:
        *pack_op = (SwsPackOp) {{ 10, 10, 10, 2 }};
        *rw_op = (SwsReadWriteOp) { .elems = 1, .packed = true };
        return 0;
    /* 3 component formats with one channel ignored */
    case AV_PIX_FMT_RGB0:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_0RGB:
    case AV_PIX_FMT_0BGR:
    case AV_PIX_FMT_XV36BE:
    case AV_PIX_FMT_XV36LE:
    case AV_PIX_FMT_XV48BE:
    case AV_PIX_FMT_XV48LE:
    case AV_PIX_FMT_VUYX:
        *pack_op = (SwsPackOp) {0};
        *rw_op = (SwsReadWriteOp) { .elems = 4, .packed = true };
        return 0;
    /* Unpacked byte-aligned 4:4:4 formats */
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUV444P9BE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUV444P10BE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV444P12BE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV444P14BE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV444P16BE:
    case AV_PIX_FMT_YUV444P16LE:
    case AV_PIX_FMT_YUV444P10MSBBE:
    case AV_PIX_FMT_YUV444P10MSBLE:
    case AV_PIX_FMT_YUV444P12MSBBE:
    case AV_PIX_FMT_YUV444P12MSBLE:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_YUVA444P9BE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUVA444P10BE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUVA444P12BE:
    case AV_PIX_FMT_YUVA444P12LE:
    case AV_PIX_FMT_YUVA444P16BE:
    case AV_PIX_FMT_YUVA444P16LE:
    case AV_PIX_FMT_AYUV:
    case AV_PIX_FMT_UYVA:
    case AV_PIX_FMT_VYU444:
    case AV_PIX_FMT_AYUV64BE:
    case AV_PIX_FMT_AYUV64LE:
    case AV_PIX_FMT_VUYA:
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGB48BE:
    case AV_PIX_FMT_RGB48LE:
    case AV_PIX_FMT_BGR48BE:
    case AV_PIX_FMT_BGR48LE:
    //case AV_PIX_FMT_RGB96BE: TODO: AVRational can't fit 2^32-1
    //case AV_PIX_FMT_RGB96LE:
    //case AV_PIX_FMT_RGBF16BE: TODO: no support for float16 currently
    //case AV_PIX_FMT_RGBF16LE:
    case AV_PIX_FMT_RGBF32BE:
    case AV_PIX_FMT_RGBF32LE:
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_RGBA64BE:
    case AV_PIX_FMT_RGBA64LE:
    case AV_PIX_FMT_BGRA64BE:
    case AV_PIX_FMT_BGRA64LE:
    //case AV_PIX_FMT_RGBA128BE: TODO: AVRational can't fit 2^32-1
    //case AV_PIX_FMT_RGBA128LE:
    case AV_PIX_FMT_RGBAF32BE:
    case AV_PIX_FMT_RGBAF32LE:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRP9BE:
    case AV_PIX_FMT_GBRP9LE:
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_GBRP14BE:
    case AV_PIX_FMT_GBRP14LE:
    case AV_PIX_FMT_GBRP16BE:
    case AV_PIX_FMT_GBRP16LE:
    //case AV_PIX_FMT_GBRPF16BE: TODO
    //case AV_PIX_FMT_GBRPF16LE:
    case AV_PIX_FMT_GBRP10MSBBE:
    case AV_PIX_FMT_GBRP10MSBLE:
    case AV_PIX_FMT_GBRP12MSBBE:
    case AV_PIX_FMT_GBRP12MSBLE:
    case AV_PIX_FMT_GBRPF32BE:
    case AV_PIX_FMT_GBRPF32LE:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRAP10BE:
    case AV_PIX_FMT_GBRAP10LE:
    case AV_PIX_FMT_GBRAP12BE:
    case AV_PIX_FMT_GBRAP12LE:
    case AV_PIX_FMT_GBRAP14BE:
    case AV_PIX_FMT_GBRAP14LE:
    case AV_PIX_FMT_GBRAP16BE:
    case AV_PIX_FMT_GBRAP16LE:
    //case AV_PIX_FMT_GBRAPF16BE: TODO
    //case AV_PIX_FMT_GBRAPF16LE:
    case AV_PIX_FMT_GBRAPF32BE:
    case AV_PIX_FMT_GBRAPF32LE:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY9BE:
    case AV_PIX_FMT_GRAY9LE:
    case AV_PIX_FMT_GRAY10BE:
    case AV_PIX_FMT_GRAY10LE:
    case AV_PIX_FMT_GRAY12BE:
    case AV_PIX_FMT_GRAY12LE:
    case AV_PIX_FMT_GRAY14BE:
    case AV_PIX_FMT_GRAY14LE:
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_GRAY16LE:
    //case AV_PIX_FMT_GRAYF16BE: TODO
    //case AV_PIX_FMT_GRAYF16LE:
    //case AV_PIX_FMT_YAF16BE:
    //case AV_PIX_FMT_YAF16LE:
    case AV_PIX_FMT_GRAYF32BE:
    case AV_PIX_FMT_GRAYF32LE:
    case AV_PIX_FMT_YAF32BE:
    case AV_PIX_FMT_YAF32LE:
    case AV_PIX_FMT_YA8:
    case AV_PIX_FMT_YA16LE:
    case AV_PIX_FMT_YA16BE:
        *pack_op = (SwsPackOp) {0};
        *rw_op = (SwsReadWriteOp) {
            .elems  = desc->nb_components,
            .packed = desc->nb_components > 1 && !(desc->flags & AV_PIX_FMT_FLAG_PLANAR),
        };
        return 0;
    }

    return AVERROR(ENOTSUP);
}

static SwsPixelType get_packed_type(SwsPackOp pack)
{
    const int sum = pack.pattern[0] + pack.pattern[1] +
                    pack.pattern[2] + pack.pattern[3];
    if (sum > 16)
        return SWS_PIXEL_U32;
    else if (sum > 8)
        return SWS_PIXEL_U16;
    else
        return SWS_PIXEL_U8;
}

#if HAVE_BIGENDIAN
#  define NATIVE_ENDIAN_FLAG AV_PIX_FMT_FLAG_BE
#else
#  define NATIVE_ENDIAN_FLAG 0
#endif

int ff_sws_decode_pixfmt(SwsOpList *ops, enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    SwsPixelType pixel_type = fmt_pixel_type(fmt);
    SwsPixelType raw_type = pixel_type;
    SwsReadWriteOp rw_op;
    SwsPackOp unpack;

    RET(fmt_read_write(fmt, &rw_op, &unpack));
    if (unpack.pattern[0])
        raw_type = get_packed_type(unpack);

    /* TODO: handle subsampled or semipacked input formats */
    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op   = SWS_OP_READ,
        .type = raw_type,
        .rw   = rw_op,
    }));

    if ((desc->flags & AV_PIX_FMT_FLAG_BE) != NATIVE_ENDIAN_FLAG) {
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_SWAP_BYTES,
            .type = raw_type,
        }));
    }

    if (unpack.pattern[0]) {
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_UNPACK,
            .type = raw_type,
            .pack = unpack,
        }));

        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_CONVERT,
            .type = raw_type,
            .convert.to = pixel_type,
        }));
    }

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op      = SWS_OP_SWIZZLE,
        .type    = pixel_type,
        .swizzle = swizzle_inv(fmt_swizzle(fmt)),
    }));

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op   = SWS_OP_RSHIFT,
        .type = pixel_type,
        .c.u  = fmt_shift(fmt),
    }));

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op   = SWS_OP_CLEAR,
        .type = pixel_type,
        .c    = fmt_clear(fmt),
    }));

    return 0;
}

int ff_sws_encode_pixfmt(SwsOpList *ops, enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    SwsPixelType pixel_type = fmt_pixel_type(fmt);
    SwsPixelType raw_type = pixel_type;
    SwsReadWriteOp rw_op;
    SwsPackOp pack;

    RET(fmt_read_write(fmt, &rw_op, &pack));
    if (pack.pattern[0])
        raw_type = get_packed_type(pack);

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op   = SWS_OP_LSHIFT,
        .type = pixel_type,
        .c.u  = fmt_shift(fmt),
    }));

    if (rw_op.elems > desc->nb_components) {
        /* Format writes unused alpha channel, clear it explicitly for sanity */
        av_assert1(!(desc->flags & AV_PIX_FMT_FLAG_ALPHA));
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_CLEAR,
            .type = pixel_type,
            .c.q4[3] = Q0,
        }));
    }

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op      = SWS_OP_SWIZZLE,
        .type    = pixel_type,
        .swizzle = fmt_swizzle(fmt),
    }));

    if (pack.pattern[0]) {
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_CONVERT,
            .type = pixel_type,
            .convert.to = raw_type,
        }));

        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_PACK,
            .type = raw_type,
            .pack = pack,
        }));
    }

    if ((desc->flags & AV_PIX_FMT_FLAG_BE) != NATIVE_ENDIAN_FLAG) {
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_SWAP_BYTES,
            .type = raw_type,
        }));
    }

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op   = SWS_OP_WRITE,
        .type = raw_type,
        .rw   = rw_op,
    }));
    return 0;
}

static inline AVRational av_neg_q(AVRational x)
{
    return (AVRational) { -x.num, x.den };
}

static SwsLinearOp fmt_encode_range(const SwsFormat fmt, bool *incomplete)
{
    SwsLinearOp c = { .m = {
        { Q1, Q0, Q0, Q0, Q0 },
        { Q0, Q1, Q0, Q0, Q0 },
        { Q0, Q0, Q1, Q0, Q0 },
        { Q0, Q0, Q0, Q1, Q0 },
    }};

    const int depth0 = fmt.desc->comp[0].depth;
    const int depth1 = fmt.desc->comp[1].depth;
    const int depth2 = fmt.desc->comp[2].depth;
    const int depth3 = fmt.desc->comp[3].depth;

    if (fmt.desc->flags & AV_PIX_FMT_FLAG_FLOAT)
        return c; /* floats are directly output as-is */

    if (fmt.csp == AVCOL_SPC_RGB || (fmt.desc->flags & AV_PIX_FMT_FLAG_XYZ)) {
        c.m[0][0] = Q((1 << depth0) - 1);
        c.m[1][1] = Q((1 << depth1) - 1);
        c.m[2][2] = Q((1 << depth2) - 1);
    } else if (fmt.range == AVCOL_RANGE_JPEG) {
        /* Full range YUV */
        c.m[0][0] = Q((1 << depth0) - 1);
        if (fmt.desc->nb_components >= 3) {
            /* This follows the ITU-R convention, which is slightly different
             * from the JFIF convention. */
            c.m[1][1] = Q((1 << depth1) - 1);
            c.m[2][2] = Q((1 << depth2) - 1);
            c.m[1][4] = Q(1 << (depth1 - 1));
            c.m[2][4] = Q(1 << (depth2 - 1));
        }
    } else {
        /* Limited range YUV */
        if (fmt.range == AVCOL_RANGE_UNSPECIFIED)
            *incomplete = true;
        c.m[0][0] = Q(219 << (depth0 - 8));
        c.m[0][4] = Q( 16 << (depth0 - 8));
        if (fmt.desc->nb_components >= 3) {
            c.m[1][1] = Q(224 << (depth1 - 8));
            c.m[2][2] = Q(224 << (depth2 - 8));
            c.m[1][4] = Q(128 << (depth1 - 8));
            c.m[2][4] = Q(128 << (depth2 - 8));
        }
    }

    if (fmt.desc->flags & AV_PIX_FMT_FLAG_ALPHA) {
        const bool is_ya = fmt.desc->nb_components == 2;
        c.m[3][3] = Q((1 << (is_ya ? depth1 : depth3)) - 1);
    }

    if (fmt.format == AV_PIX_FMT_MONOWHITE) {
        /* This format is inverted, 0 = white, 1 = black */
        c.m[0][4] = av_add_q(c.m[0][4], c.m[0][0]);
        c.m[0][0] = av_neg_q(c.m[0][0]);
    }

    c.mask = ff_sws_linear_mask(c);
    return c;
}

static SwsLinearOp fmt_decode_range(const SwsFormat fmt, bool *incomplete)
{
    SwsLinearOp c = fmt_encode_range(fmt, incomplete);

    /* Invert main diagonal + offset: x = s * y + k  ==>  y = (x - k) / s */
    for (int i = 0; i < 4; i++) {
        c.m[i][i] = av_inv_q(c.m[i][i]);
        c.m[i][4] = av_mul_q(c.m[i][4], av_neg_q(c.m[i][i]));
    }

    /* Explicitly initialize alpha for sanity */
    if (!(fmt.desc->flags & AV_PIX_FMT_FLAG_ALPHA))
        c.m[3][4] = Q1;

    c.mask = ff_sws_linear_mask(c);
    return c;
}

static AVRational *generate_bayer_matrix(const int size_log2)
{
    const int size = 1 << size_log2;
    const int num_entries = size * size;
    AVRational *m = av_refstruct_allocz(sizeof(*m) * num_entries);
    av_assert1(size_log2 < 16);
    if (!m)
        return NULL;

    /* Start with a 1x1 matrix */
    m[0] = Q0;

    /* Generate three copies of the current, appropriately scaled and offset */
    for (int sz = 1; sz < size; sz <<= 1) {
        const int den = 4 * sz * sz;
        for (int y = 0; y < sz; y++) {
            for (int x = 0; x < sz; x++) {
                const AVRational cur = m[y * size + x];
                m[(y + sz) * size + x + sz] = av_add_q(cur, av_make_q(1, den));
                m[(y     ) * size + x + sz] = av_add_q(cur, av_make_q(2, den));
                m[(y + sz) * size + x     ] = av_add_q(cur, av_make_q(3, den));
            }
        }
    }

    /**
     * To correctly round, we need to evenly distribute the result on [0, 1),
     * giving an average value of 1/2.
     *
     * After the above construction, we have a matrix with average value:
     *   [ 0/N + 1/N + 2/N + ... (N-1)/N ] / N = (N-1)/(2N)
     * where N = size * size is the total number of entries.
     *
     * To make the average value equal to 1/2 = N/(2N), add a bias of 1/(2N).
     */
    for (int i = 0; i < num_entries; i++)
        m[i] = av_add_q(m[i], av_make_q(1, 2 * num_entries));

    return m;
}

static bool trc_is_hdr(enum AVColorTransferCharacteristic trc)
{
    static_assert(AVCOL_TRC_NB == 19, "Update this list when adding TRCs");
    switch (trc) {
    case AVCOL_TRC_LOG:
    case AVCOL_TRC_LOG_SQRT:
    case AVCOL_TRC_SMPTEST2084:
    case AVCOL_TRC_ARIB_STD_B67:
        return true;
    default:
        return false;
    }
}

static int fmt_dither(SwsContext *ctx, SwsOpList *ops,
                      const SwsPixelType type, const SwsFormat fmt)
{
    SwsDither mode = ctx->dither;
    SwsDitherOp dither;

    if (mode == SWS_DITHER_AUTO) {
        /* Visual threshold of perception: 12 bits for SDR, 14 bits for HDR */
        const int jnd_bits = trc_is_hdr(fmt.color.trc) ? 14 : 12;
        const int bpc = fmt.desc->comp[0].depth;
        mode = bpc >= jnd_bits ? SWS_DITHER_NONE : SWS_DITHER_BAYER;
    }

    switch (mode) {
    case SWS_DITHER_NONE:
        if (ctx->flags & SWS_ACCURATE_RND) {
            /* Add constant 0.5 for correct rounding */
            AVRational *bias = av_refstruct_allocz(sizeof(*bias));
            if (!bias)
                return AVERROR(ENOMEM);
            *bias = (AVRational) {1, 2};
            return ff_sws_op_list_append(ops, &(SwsOp) {
                .op   = SWS_OP_DITHER,
                .type = type,
                .dither.matrix = bias,
            });
        } else {
            return 0; /* No-op */
        }
    case SWS_DITHER_BAYER:
        /* Hardcode 16x16 matrix for now; in theory we could adjust this
         * based on the expected level of precision in the output, since lower
         * bit depth outputs can suffice with smaller dither matrices; however
         * in practice we probably want to use error diffusion for such low bit
         * depths anyway */
        dither.size_log2 = 4;
        dither.matrix = generate_bayer_matrix(dither.size_log2);
        if (!dither.matrix)
            return AVERROR(ENOMEM);
        return ff_sws_op_list_append(ops, &(SwsOp) {
            .op     = SWS_OP_DITHER,
            .type   = type,
            .dither = dither,
        });
    case SWS_DITHER_ED:
    case SWS_DITHER_A_DITHER:
    case SWS_DITHER_X_DITHER:
        return AVERROR(ENOTSUP);

    case SWS_DITHER_NB:
        break;
    }

    av_unreachable("Invalid dither mode");
    return AVERROR(EINVAL);
}

static inline SwsLinearOp
linear_mat3(const AVRational m00, const AVRational m01, const AVRational m02,
            const AVRational m10, const AVRational m11, const AVRational m12,
            const AVRational m20, const AVRational m21, const AVRational m22)
{
    SwsLinearOp c = {{
        { m00, m01, m02, Q0, Q0 },
        { m10, m11, m12, Q0, Q0 },
        { m20, m21, m22, Q0, Q0 },
        {  Q0,  Q0,  Q0, Q1, Q0 },
    }};

    c.mask = ff_sws_linear_mask(c);
    return c;
}

int ff_sws_decode_colors(SwsContext *ctx, SwsPixelType type,
                         SwsOpList *ops, const SwsFormat fmt, bool *incomplete)
{
    const AVLumaCoefficients *c = av_csp_luma_coeffs_from_avcsp(fmt.csp);

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .op         = SWS_OP_CONVERT,
        .type       = fmt_pixel_type(fmt.format),
        .convert.to = type,
    }));

    /* Decode pixel format into standardized range */
    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .type = type,
        .op   = SWS_OP_LINEAR,
        .lin  = fmt_decode_range(fmt, incomplete),
    }));

    /* Final step, decode colorspace */
    switch (fmt.csp) {
    case AVCOL_SPC_RGB:
        return 0;
    case AVCOL_SPC_UNSPECIFIED:
        c = av_csp_luma_coeffs_from_avcsp(AVCOL_SPC_BT470BG);
        *incomplete = true;
        /* fall through */
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_BT2020_NCL: {
        AVRational crg = av_sub_q(Q0, av_div_q(c->cr, c->cg));
        AVRational cbg = av_sub_q(Q0, av_div_q(c->cb, c->cg));
        AVRational m02 = av_mul_q(Q(2), av_sub_q(Q1, c->cr));
        AVRational m21 = av_mul_q(Q(2), av_sub_q(Q1, c->cb));
        AVRational m11 = av_mul_q(cbg, m21);
        AVRational m12 = av_mul_q(crg, m02);

        return ff_sws_op_list_append(ops, &(SwsOp) {
            .type = type,
            .op   = SWS_OP_LINEAR,
            .lin  = linear_mat3(
                Q1,  Q0, m02,
                Q1, m11, m12,
                Q1, m21,  Q0
            ),
        });
    }

    case AVCOL_SPC_YCGCO:
        return ff_sws_op_list_append(ops, &(SwsOp) {
            .type = type,
            .op   = SWS_OP_LINEAR,
            .lin  = linear_mat3(
                Q1, Q(-1), Q( 1),
                Q1, Q( 1), Q( 0),
                Q1, Q(-1), Q(-1)
            ),
        });

    case AVCOL_SPC_BT2020_CL:
    case AVCOL_SPC_SMPTE2085:
    case AVCOL_SPC_CHROMA_DERIVED_NCL:
    case AVCOL_SPC_CHROMA_DERIVED_CL:
    case AVCOL_SPC_ICTCP:
    case AVCOL_SPC_IPT_C2:
    case AVCOL_SPC_YCGCO_RE:
    case AVCOL_SPC_YCGCO_RO:
        return AVERROR(ENOTSUP);

    case AVCOL_SPC_RESERVED:
        return AVERROR(EINVAL);

    case AVCOL_SPC_NB:
        break;
    }

    av_unreachable("Corrupt AVColorSpace value?");
    return AVERROR(EINVAL);
}

int ff_sws_encode_colors(SwsContext *ctx, SwsPixelType type,
                         SwsOpList *ops, const SwsFormat fmt, bool *incomplete)
{
    const AVLumaCoefficients *c = av_csp_luma_coeffs_from_avcsp(fmt.csp);

    switch (fmt.csp) {
    case AVCOL_SPC_RGB:
        break;
    case AVCOL_SPC_UNSPECIFIED:
        c = av_csp_luma_coeffs_from_avcsp(AVCOL_SPC_BT470BG);
        *incomplete = true;
        /* fall through */
    case AVCOL_SPC_FCC:
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_SMPTE240M:
    case AVCOL_SPC_BT2020_NCL: {
        AVRational cb1 = av_sub_q(c->cb, Q1);
        AVRational cr1 = av_sub_q(c->cr, Q1);
        AVRational m20 = av_make_q(1,2);
        AVRational m10 = av_mul_q(m20, av_div_q(c->cr, cb1));
        AVRational m11 = av_mul_q(m20, av_div_q(c->cg, cb1));
        AVRational m21 = av_mul_q(m20, av_div_q(c->cg, cr1));
        AVRational m22 = av_mul_q(m20, av_div_q(c->cb, cr1));

        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .type = type,
            .op   = SWS_OP_LINEAR,
            .lin  = linear_mat3(
                c->cr, c->cg, c->cb,
                m10,     m11,   m20,
                m20,     m21,   m22
            ),
        }));
        break;
    }

    case AVCOL_SPC_YCGCO:
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .type = type,
            .op   = SWS_OP_LINEAR,
            .lin  = linear_mat3(
                av_make_q( 1, 4), av_make_q(1, 2), av_make_q( 1, 4),
                av_make_q( 1, 2), av_make_q(0, 1), av_make_q(-1, 2),
                av_make_q(-1, 4), av_make_q(1, 2), av_make_q(-1, 4)
            ),
        }));
        break;

    case AVCOL_SPC_BT2020_CL:
    case AVCOL_SPC_SMPTE2085:
    case AVCOL_SPC_CHROMA_DERIVED_NCL:
    case AVCOL_SPC_CHROMA_DERIVED_CL:
    case AVCOL_SPC_ICTCP:
    case AVCOL_SPC_IPT_C2:
    case AVCOL_SPC_YCGCO_RE:
    case AVCOL_SPC_YCGCO_RO:
        return AVERROR(ENOTSUP);

    case AVCOL_SPC_RESERVED:
    case AVCOL_SPC_NB:
        return AVERROR(EINVAL);
    }

    RET(ff_sws_op_list_append(ops, &(SwsOp) {
        .type = type,
        .op   = SWS_OP_LINEAR,
        .lin  = fmt_encode_range(fmt, incomplete),
    }));

    if (!(fmt.desc->flags & AV_PIX_FMT_FLAG_FLOAT)) {
        SwsConst range = {0};

        const bool is_ya = fmt.desc->nb_components == 2;
        for (int i = 0; i < fmt.desc->nb_components; i++) {
            /* Clamp to legal pixel range */
            const int idx = i * (is_ya ? 3 : 1);
            range.q4[idx] = Q((1 << fmt.desc->comp[i].depth) - 1);
        }

        RET(fmt_dither(ctx, ops, type, fmt));
        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_MAX,
            .type = type,
            .c.q4 = { Q0, Q0, Q0, Q0 },
        }));

        RET(ff_sws_op_list_append(ops, &(SwsOp) {
            .op   = SWS_OP_MIN,
            .type = type,
            .c    = range,
        }));
    }

    return ff_sws_op_list_append(ops, &(SwsOp) {
        .type       = type,
        .op         = SWS_OP_CONVERT,
        .convert.to = fmt_pixel_type(fmt.format),
    });
}

#endif /* CONFIG_UNSTABLE */
