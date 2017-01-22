/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003-2011 Michael Niedermayer <michaelni@gmx.at>
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
 * H.264 / AVC / MPEG-4 part10 DSP functions.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "bit_depth_template.c"

static void FUNCC(ff_h264_add_pixels4)(uint8_t *_dst, int16_t *_src, int stride)
{
    int i;
    pixel *dst = (pixel *) _dst;
    dctcoef *src = (dctcoef *) _src;
    stride /= sizeof(pixel);

    for (i = 0; i < 4; i++) {
        dst[0] += src[0];
        dst[1] += src[1];
        dst[2] += src[2];
        dst[3] += src[3];

        dst += stride;
        src += 4;
    }

    memset(_src, 0, sizeof(dctcoef) * 16);
}

static void FUNCC(ff_h264_add_pixels8)(uint8_t *_dst, int16_t *_src, int stride)
{
    int i;
    pixel *dst = (pixel *) _dst;
    dctcoef *src = (dctcoef *) _src;
    stride /= sizeof(pixel);

    for (i = 0; i < 8; i++) {
        dst[0] += src[0];
        dst[1] += src[1];
        dst[2] += src[2];
        dst[3] += src[3];
        dst[4] += src[4];
        dst[5] += src[5];
        dst[6] += src[6];
        dst[7] += src[7];

        dst += stride;
        src += 8;
    }

    memset(_src, 0, sizeof(dctcoef) * 64);
}
