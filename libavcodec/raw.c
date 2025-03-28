/*
 * Raw Video Codec
 * Copyright (c) 2001 Fabrice Bellard
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

/**
 * @file
 * Raw Video Codec
 */

#include "avcodec.h"
#include "raw.h"
#include "raw_pix_fmt_tags.h"

unsigned int avcodec_pix_fmt_to_codec_tag(enum AVPixelFormat fmt)
{
    const PixelFormatTag *tags = raw_pix_fmt_tags;
    while (tags->pix_fmt >= 0) {
        if (tags->pix_fmt == fmt)
            return tags->fourcc;
        tags++;
    }
    return 0;
}

static const PixelFormatTag pix_fmt_bps_avi[] = {
    { AV_PIX_FMT_PAL8,    1 },
    { AV_PIX_FMT_PAL8,    2 },
    { AV_PIX_FMT_PAL8,    4 },
    { AV_PIX_FMT_PAL8,    8 },
    { AV_PIX_FMT_RGB444LE, 12 },
    { AV_PIX_FMT_RGB555LE, 15 },
    { AV_PIX_FMT_RGB555LE, 16 },
    { AV_PIX_FMT_BGR24,  24 },
    { AV_PIX_FMT_BGRA,   32 },
    { AV_PIX_FMT_NONE,    0 },
};

static const PixelFormatTag pix_fmt_bps_mov[] = {
    { AV_PIX_FMT_PAL8,      1 },
    { AV_PIX_FMT_PAL8,      2 },
    { AV_PIX_FMT_PAL8,      4 },
    { AV_PIX_FMT_PAL8,      8 },
    { AV_PIX_FMT_RGB555BE, 16 },
    { AV_PIX_FMT_RGB24,    24 },
    { AV_PIX_FMT_ARGB,     32 },
    { AV_PIX_FMT_PAL8,     33 },
    { AV_PIX_FMT_NONE,      0 },
};

static enum AVPixelFormat find_pix_fmt(const PixelFormatTag *tags,
                                       unsigned int fourcc)
{
    while (tags->pix_fmt != AV_PIX_FMT_NONE) {
        if (tags->fourcc == fourcc)
            return tags->pix_fmt;
        tags++;
    }
    return AV_PIX_FMT_NONE;
}

enum AVPixelFormat avpriv_pix_fmt_find(enum PixelFormatTagLists list,
                                       unsigned fourcc)
{
    const PixelFormatTag *tags;

    switch (list) {
    case PIX_FMT_LIST_RAW:
        tags = raw_pix_fmt_tags;
        break;
    case PIX_FMT_LIST_AVI:
        tags = pix_fmt_bps_avi;
        break;
    case PIX_FMT_LIST_MOV:
        tags = pix_fmt_bps_mov;
        break;
    }
    return find_pix_fmt(tags, fourcc);
}
