/*
 * Copyright (c) 2002-2012 Michael Niedermayer
 * Copyright (C) 2012 Ronald S. Bultje
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

#include "bit_depth_template.c"
void FUNC(ff_emulated_edge_mc)(uint8_t *buf, const uint8_t *src,
                                      ptrdiff_t linesize_arg,
                                      int block_w, int block_h,
                                      int src_x, int src_y, int w, int h)
{
    int x, y;
    int start_y, start_x, end_y, end_x;
    emuedge_linesize_type linesize = linesize_arg;

    if (!w || !h)
        return;

    if (src_y >= h) {
        src -= src_y * linesize;
        src += (h - 1) * linesize;
        src_y = h - 1;
    } else if (src_y <= -block_h) {
        src -= src_y * linesize;
        src += (1 - block_h) * linesize;
        src_y = 1 - block_h;
    }
    if (src_x >= w) {
        src  += (w - 1 - src_x) * sizeof(pixel);
        src_x = w - 1;
    } else if (src_x <= -block_w) {
        src  += (1 - block_w - src_x) * sizeof(pixel);
        src_x = 1 - block_w;
    }

    start_y = FFMAX(0, -src_y);
    start_x = FFMAX(0, -src_x);
    end_y = FFMIN(block_h, h-src_y);
    end_x = FFMIN(block_w, w-src_x);
    av_assert2(start_y < end_y && block_h);
    av_assert2(start_x < end_x && block_w);

    w    = end_x - start_x;
    src += start_y * linesize + start_x * sizeof(pixel);
    buf += start_x * sizeof(pixel);

    // top
    for (y = 0; y < start_y; y++) {
        memcpy(buf, src, w * sizeof(pixel));
        buf += linesize;
    }

    // copy existing part
    for (; y < end_y; y++) {
        memcpy(buf, src, w * sizeof(pixel));
        src += linesize;
        buf += linesize;
    }

    // bottom
    src -= linesize;
    for (; y < block_h; y++) {
        memcpy(buf, src, w * sizeof(pixel));
        buf += linesize;
    }

    buf -= block_h * linesize + start_x * sizeof(pixel);
    while (block_h--) {
        pixel *bufp = (pixel *) buf;

        // left
        for(x = 0; x < start_x; x++) {
            bufp[x] = bufp[start_x];
        }

        // right
        for (x = end_x; x < block_w; x++) {
            bufp[x] = bufp[end_x - 1];
        }
        buf += linesize;
    }
}
