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

#ifndef SWSCALE_UTILS_H
#define SWSCALE_UTILS_H

#include "libavutil/pixdesc.h"

#include "swscale.h"

enum {
    FIELD_TOP, /* top/even rows, or progressive */
    FIELD_BOTTOM, /* bottom/odd rows */
};

/* Subset of AVFrame parameters that uniquely determine pixel representation */
typedef struct SwsFormat {
    int width, height;
    int interlaced;
    enum AVPixelFormat format;
    enum AVColorRange range;
    enum AVColorPrimaries prim;
    enum AVColorTransferCharacteristic trc;
    enum AVColorSpace csp;
    enum AVChromaLocation loc;
    const AVPixFmtDescriptor *desc; /* convenience */
} SwsFormat;

/**
 * This function also sanitizes and strips the input data, removing irrelevant
 * fields for certain formats.
 */
SwsFormat ff_fmt_from_frame(const AVFrame *frame, int field);

static inline int ff_fmt_equal(const SwsFormat *fmt1, const SwsFormat *fmt2)
{
    return fmt1->width      == fmt2->width      &&
           fmt1->height     == fmt2->height     &&
           fmt1->interlaced == fmt2->interlaced &&
           fmt1->format     == fmt2->format     &&
           fmt1->range      == fmt2->range      &&
           fmt1->prim       == fmt2->prim       &&
           fmt1->trc        == fmt2->trc        &&
           fmt1->csp        == fmt2->csp        &&
           fmt1->loc        == fmt2->loc;
}

static inline int ff_fmt_align(enum AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (desc->flags & AV_PIX_FMT_FLAG_BAYER) {
        return 2;
    } else {
        return 1 << desc->log2_chroma_h;
    }
}

int ff_test_fmt(const SwsFormat *fmt, int output);

#endif /* SWSCALE_UTILS_H */
