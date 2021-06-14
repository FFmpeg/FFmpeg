/*
 * H.261 common code
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * H.261 codec
 */

#include "h261.h"
#include "mpegvideo.h"

#define IS_FIL(a)    ((a) & MB_TYPE_H261_FIL)

static void h261_loop_filter(uint8_t *src, int stride)
{
    int x, y, xy, yz;
    int temp[64];

    for (x = 0; x < 8; x++) {
        temp[x]         = 4 * src[x];
        temp[x + 7 * 8] = 4 * src[x + 7 * stride];
    }
    for (y = 1; y < 7; y++) {
        for (x = 0; x < 8; x++) {
            xy       = y * stride + x;
            yz       = y * 8      + x;
            temp[yz] = src[xy - stride] + 2 * src[xy] + src[xy + stride];
        }
    }

    for (y = 0; y < 8; y++) {
        src[y * stride]     = (temp[y * 8]     + 2) >> 2;
        src[y * stride + 7] = (temp[y * 8 + 7] + 2) >> 2;
        for (x = 1; x < 7; x++) {
            xy      = y * stride + x;
            yz      = y * 8      + x;
            src[xy] = (temp[yz - 1] + 2 * temp[yz] + temp[yz + 1] + 8) >> 4;
        }
    }
}

void ff_h261_loop_filter(MpegEncContext *s)
{
    H261Context *h       = (H261Context *)s;
    const int linesize   = s->linesize;
    const int uvlinesize = s->uvlinesize;
    uint8_t *dest_y      = s->dest[0];
    uint8_t *dest_cb     = s->dest[1];
    uint8_t *dest_cr     = s->dest[2];

    if (!(IS_FIL(h->mtype)))
        return;

    h261_loop_filter(dest_y,                    linesize);
    h261_loop_filter(dest_y + 8,                linesize);
    h261_loop_filter(dest_y + 8 * linesize,     linesize);
    h261_loop_filter(dest_y + 8 * linesize + 8, linesize);
    h261_loop_filter(dest_cb, uvlinesize);
    h261_loop_filter(dest_cr, uvlinesize);
}
