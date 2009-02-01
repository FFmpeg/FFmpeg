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
 * @file libavcodec/raw.c
 * Raw Video Codec
 */

#include "avcodec.h"
#include "raw.h"

const PixelFormatTag ff_raw_pixelFormatTags[] = {
    { PIX_FMT_YUV420P, MKTAG('I', '4', '2', '0') }, /* Planar formats */
    { PIX_FMT_YUV420P, MKTAG('I', 'Y', 'U', 'V') },
    { PIX_FMT_YUV420P, MKTAG('Y', 'V', '1', '2') },
    { PIX_FMT_YUV410P, MKTAG('Y', 'U', 'V', '9') },
    { PIX_FMT_YUV411P, MKTAG('Y', '4', '1', 'B') },
    { PIX_FMT_YUV422P, MKTAG('Y', '4', '2', 'B') },
    { PIX_FMT_GRAY8,   MKTAG('Y', '8', '0', '0') },
    { PIX_FMT_GRAY8,   MKTAG(' ', ' ', 'Y', '8') },


    { PIX_FMT_YUYV422, MKTAG('Y', 'U', 'Y', '2') }, /* Packed formats */
    { PIX_FMT_YUYV422, MKTAG('Y', '4', '2', '2') },
    { PIX_FMT_UYVY422, MKTAG('U', 'Y', 'V', 'Y') },
    { PIX_FMT_UYVY422, MKTAG('H', 'D', 'Y', 'C') },
    { PIX_FMT_GRAY8,   MKTAG('G', 'R', 'E', 'Y') },
    { PIX_FMT_RGB555,  MKTAG('R', 'G', 'B', 15) },
    { PIX_FMT_BGR555,  MKTAG('B', 'G', 'R', 15) },
    { PIX_FMT_RGB565,  MKTAG('R', 'G', 'B', 16) },
    { PIX_FMT_BGR565,  MKTAG('B', 'G', 'R', 16) },

    /* quicktime */
    { PIX_FMT_UYVY422, MKTAG('2', 'v', 'u', 'y') },
    { PIX_FMT_UYVY422, MKTAG('A', 'V', 'U', 'I') }, /* FIXME merge both fields */
    { PIX_FMT_PAL8,    MKTAG('W', 'R', 'A', 'W') },

    { PIX_FMT_NONE, 0 },
};

unsigned int avcodec_pix_fmt_to_codec_tag(enum PixelFormat fmt)
{
    const PixelFormatTag * tags = ff_raw_pixelFormatTags;
    while (tags->pix_fmt >= 0) {
        if (tags->pix_fmt == fmt)
            return tags->fourcc;
        tags++;
    }
    return 0;
}
