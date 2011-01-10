/*
 * Copyright (C) 2003 Mike Melanson
 * Copyright (C) 2003 Dr. Tim Ferguson
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
 * id RoQ Video common functions based on work by Dr. Tim Ferguson
 */

#include "avcodec.h"
#include "roqvideo.h"

static inline void block_copy(unsigned char *out, unsigned char *in,
                              int outstride, int instride, int sz)
{
    int rows = sz;
    while(rows--) {
        memcpy(out, in, sz);
        out += outstride;
        in += instride;
    }
}

void ff_apply_vector_2x2(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned char *bptr;
    int boffs,stride;

    stride = ri->current_frame->linesize[0];
    boffs = y*stride + x;

    bptr = ri->current_frame->data[0] + boffs;
    bptr[0       ] = cell->y[0];
    bptr[1       ] = cell->y[1];
    bptr[stride  ] = cell->y[2];
    bptr[stride+1] = cell->y[3];

    stride = ri->current_frame->linesize[1];
    boffs = y*stride + x;

    bptr = ri->current_frame->data[1] + boffs;
    bptr[0       ] =
    bptr[1       ] =
    bptr[stride  ] =
    bptr[stride+1] = cell->u;

    bptr = ri->current_frame->data[2] + boffs;
    bptr[0       ] =
    bptr[1       ] =
    bptr[stride  ] =
    bptr[stride+1] = cell->v;
}

void ff_apply_vector_4x4(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned char *bptr;
    int boffs,stride;

    stride = ri->current_frame->linesize[0];
    boffs = y*stride + x;

    bptr = ri->current_frame->data[0] + boffs;
    bptr[         0] = bptr[         1] = bptr[stride    ] = bptr[stride  +1] = cell->y[0];
    bptr[         2] = bptr[         3] = bptr[stride  +2] = bptr[stride  +3] = cell->y[1];
    bptr[stride*2  ] = bptr[stride*2+1] = bptr[stride*3  ] = bptr[stride*3+1] = cell->y[2];
    bptr[stride*2+2] = bptr[stride*2+3] = bptr[stride*3+2] = bptr[stride*3+3] = cell->y[3];

    stride = ri->current_frame->linesize[1];
    boffs = y*stride + x;

    bptr = ri->current_frame->data[1] + boffs;
    bptr[         0] = bptr[         1] = bptr[stride    ] = bptr[stride  +1] =
    bptr[         2] = bptr[         3] = bptr[stride  +2] = bptr[stride  +3] =
    bptr[stride*2  ] = bptr[stride*2+1] = bptr[stride*3  ] = bptr[stride*3+1] =
    bptr[stride*2+2] = bptr[stride*2+3] = bptr[stride*3+2] = bptr[stride*3+3] = cell->u;

    bptr = ri->current_frame->data[2] + boffs;
    bptr[         0] = bptr[         1] = bptr[stride    ] = bptr[stride  +1] =
    bptr[         2] = bptr[         3] = bptr[stride  +2] = bptr[stride  +3] =
    bptr[stride*2  ] = bptr[stride*2+1] = bptr[stride*3  ] = bptr[stride*3+1] =
    bptr[stride*2+2] = bptr[stride*2+3] = bptr[stride*3+2] = bptr[stride*3+3] = cell->v;
}


static inline void apply_motion_generic(RoqContext *ri, int x, int y, int deltax,
                                        int deltay, int sz)
{
    int mx, my, cp;

    mx = x + deltax;
    my = y + deltay;

    /* check MV against frame boundaries */
    if ((mx < 0) || (mx > ri->width - sz) ||
        (my < 0) || (my > ri->height - sz)) {
        av_log(ri->avctx, AV_LOG_ERROR, "motion vector out of bounds: MV = (%d, %d), boundaries = (0, 0, %d, %d)\n",
            mx, my, ri->width, ri->height);
        return;
    }

    if (ri->last_frame->data[0] == NULL) {
        av_log(ri->avctx, AV_LOG_ERROR, "Invalid decode type. Invalid header?\n");
        return;
    }

    for(cp = 0; cp < 3; cp++) {
        int outstride = ri->current_frame->linesize[cp];
        int instride  = ri->last_frame   ->linesize[cp];
        block_copy(ri->current_frame->data[cp] + y*outstride + x,
                   ri->last_frame->data[cp] + my*instride + mx,
                   outstride, instride, sz);
    }
}


void ff_apply_motion_4x4(RoqContext *ri, int x, int y,
                             int deltax, int deltay)
{
    apply_motion_generic(ri, x, y, deltax, deltay, 4);
}

void ff_apply_motion_8x8(RoqContext *ri, int x, int y,
                             int deltax, int deltay)
{
    apply_motion_generic(ri, x, y, deltax, deltay, 8);
}
