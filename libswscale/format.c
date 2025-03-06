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

#include "format.h"

typedef struct FormatEntry {
    uint8_t is_supported_in         :1;
    uint8_t is_supported_out        :1;
    uint8_t is_supported_endianness :1;
} FormatEntry;

/* Format support table for legacy swscale */
static const FormatEntry format_entries[] = {
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
    [AV_PIX_FMT_GBRP]           = { 1, 1 },
    [AV_PIX_FMT_GBRP9LE]        = { 1, 1 },
    [AV_PIX_FMT_GBRP9BE]        = { 1, 1 },
    [AV_PIX_FMT_GBRP10LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP10BE]       = { 1, 1 },
    [AV_PIX_FMT_GBRAP10LE]      = { 1, 1 },
    [AV_PIX_FMT_GBRAP10BE]      = { 1, 1 },
    [AV_PIX_FMT_GBRP12LE]       = { 1, 1 },
    [AV_PIX_FMT_GBRP12BE]       = { 1, 1 },
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
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(format_entries) ?
           format_entries[pix_fmt].is_supported_in : 0;
}

int sws_isSupportedOutput(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(format_entries) ?
           format_entries[pix_fmt].is_supported_out : 0;
}

int sws_isSupportedEndiannessConversion(enum AVPixelFormat pix_fmt)
{
    return (unsigned)pix_fmt < FF_ARRAY_ELEMS(format_entries) ?
           format_entries[pix_fmt].is_supported_endianness : 0;
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

    /* Re-use the reference gamut only for "safe", similar primaries */
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
