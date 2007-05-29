/*
 * Copyright (C) 2003 the ffmpeg project
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
 *
 */

/**
 * @file roqvideo.c
 * Id RoQ Video common functions based on work by Dr. Tim Ferguson
 */

#include "avcodec.h"
#include "roqvideo.h"

#define avg2(a,b) av_clip_uint8(((int)(a)+(int)(b)+1)>>1)
#define avg4(a,b,c,d) av_clip_uint8(((int)(a)+(int)(b)+(int)(c)+(int)(d)+2)>>2)

void ff_apply_vector_2x2(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned char *yptr;

    yptr = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    *yptr++ = cell->y[0];
    *yptr++ = cell->y[1];
    yptr += (ri->y_stride - 2);
    *yptr++ = cell->y[2];
    *yptr++ = cell->y[3];
    ri->current_frame->data[1][(y/2) * (ri->c_stride) + x/2] = cell->u;
    ri->current_frame->data[2][(y/2) * (ri->c_stride) + x/2] = cell->v;
}

void ff_apply_vector_4x4(RoqContext *ri, int x, int y, roq_cell *cell)
{
    unsigned long row_inc, c_row_inc;
    register unsigned char y0, y1, u, v;
    unsigned char *yptr, *uptr, *vptr;

    yptr = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    uptr = ri->current_frame->data[1] + (y/2) * (ri->c_stride) + x/2;
    vptr = ri->current_frame->data[2] + (y/2) * (ri->c_stride) + x/2;

    row_inc = ri->y_stride - 4;
    c_row_inc = (ri->c_stride) - 2;
    *yptr++ = y0 = cell->y[0]; *uptr++ = u = cell->u; *vptr++ = v = cell->v;
    *yptr++ = y0;
    *yptr++ = y1 = cell->y[1]; *uptr++ = u; *vptr++ = v;
    *yptr++ = y1;

    yptr += row_inc;

    *yptr++ = y0;
    *yptr++ = y0;
    *yptr++ = y1;
    *yptr++ = y1;

    yptr += row_inc; uptr += c_row_inc; vptr += c_row_inc;

    *yptr++ = y0 = cell->y[2]; *uptr++ = u; *vptr++ = v;
    *yptr++ = y0;
    *yptr++ = y1 = cell->y[3]; *uptr++ = u; *vptr++ = v;
    *yptr++ = y1;

    yptr += row_inc;

    *yptr++ = y0;
    *yptr++ = y0;
    *yptr++ = y1;
    *yptr++ = y1;
}

void ff_apply_motion_4x4(RoqContext *ri, int x, int y,
                             int deltax, int deltay)
{
    int i, hw, mx, my;
    unsigned char *pa, *pb;

    mx = x + deltax;
    my = y + deltay;

    /* check MV against frame boundaries */
    if ((mx < 0) || (mx > ri->avctx->width - 4) ||
        (my < 0) || (my > ri->avctx->height - 4)) {
        av_log(ri->avctx, AV_LOG_ERROR, "motion vector out of bounds: MV = (%d, %d), boundaries = (0, 0, %d, %d)\n",
            mx, my, ri->avctx->width, ri->avctx->height);
        return;
    }

    pa = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    pb = ri->last_frame->data[0] + (my * ri->y_stride) + mx;
    for(i = 0; i < 4; i++) {
        pa[0] = pb[0];
        pa[1] = pb[1];
        pa[2] = pb[2];
        pa[3] = pb[3];
        pa += ri->y_stride;
        pb += ri->y_stride;
    }

    hw = ri->y_stride/2;
    pa = ri->current_frame->data[1] + (y * ri->y_stride)/4 + x/2;
    pb = ri->last_frame->data[1] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;

    for(i = 0; i < 2; i++) {
        switch(((my & 0x01) << 1) | (mx & 0x01)) {

        case 0:
            pa[0] = pb[0];
            pa[1] = pb[1];
            pa[hw] = pb[hw];
            pa[hw+1] = pb[hw+1];
            break;

        case 1:
            pa[0] = avg2(pb[0], pb[1]);
            pa[1] = avg2(pb[1], pb[2]);
            pa[hw] = avg2(pb[hw], pb[hw+1]);
            pa[hw+1] = avg2(pb[hw+1], pb[hw+2]);
            break;

        case 2:
            pa[0] = avg2(pb[0], pb[hw]);
            pa[1] = avg2(pb[1], pb[hw+1]);
            pa[hw] = avg2(pb[hw], pb[hw*2]);
            pa[hw+1] = avg2(pb[hw+1], pb[(hw*2)+1]);
            break;

        case 3:
            pa[0] = avg4(pb[0], pb[1], pb[hw], pb[hw+1]);
            pa[1] = avg4(pb[1], pb[2], pb[hw+1], pb[hw+2]);
            pa[hw] = avg4(pb[hw], pb[hw+1], pb[hw*2], pb[(hw*2)+1]);
            pa[hw+1] = avg4(pb[hw+1], pb[hw+2], pb[(hw*2)+1], pb[(hw*2)+1]);
            break;
        }

        pa = ri->current_frame->data[2] + (y * ri->y_stride)/4 + x/2;
        pb = ri->last_frame->data[2] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    }
}

void ff_apply_motion_8x8(RoqContext *ri, int x, int y,
                             int deltax, int deltay)
{
    int mx, my, i, j, hw;
    unsigned char *pa, *pb;

    mx = x + deltax;
    my = y + deltay;

    /* check MV against frame boundaries */
    if ((mx < 0) || (mx > ri->avctx->width - 8) ||
        (my < 0) || (my > ri->avctx->height - 8)) {
        av_log(ri->avctx, AV_LOG_ERROR, "motion vector out of bounds: MV = (%d, %d), boundaries = (0, 0, %d, %d)\n",
            mx, my, ri->avctx->width, ri->avctx->height);
        return;
    }

    pa = ri->current_frame->data[0] + (y * ri->y_stride) + x;
    pb = ri->last_frame->data[0] + (my * ri->y_stride) + mx;
    for(i = 0; i < 8; i++) {
        pa[0] = pb[0];
        pa[1] = pb[1];
        pa[2] = pb[2];
        pa[3] = pb[3];
        pa[4] = pb[4];
        pa[5] = pb[5];
        pa[6] = pb[6];
        pa[7] = pb[7];
        pa += ri->y_stride;
        pb += ri->y_stride;
    }

    hw = ri->c_stride;
    pa = ri->current_frame->data[1] + (y * ri->y_stride)/4 + x/2;
    pb = ri->last_frame->data[1] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    for(j = 0; j < 2; j++) {
        for(i = 0; i < 4; i++) {
            switch(((my & 0x01) << 1) | (mx & 0x01)) {

            case 0:
                pa[0] = pb[0];
                pa[1] = pb[1];
                pa[2] = pb[2];
                pa[3] = pb[3];
                break;

            case 1:
                pa[0] = avg2(pb[0], pb[1]);
                pa[1] = avg2(pb[1], pb[2]);
                pa[2] = avg2(pb[2], pb[3]);
                pa[3] = avg2(pb[3], pb[4]);
                break;

            case 2:
                pa[0] = avg2(pb[0], pb[hw]);
                pa[1] = avg2(pb[1], pb[hw+1]);
                pa[2] = avg2(pb[2], pb[hw+2]);
                pa[3] = avg2(pb[3], pb[hw+3]);
                break;

            case 3:
                pa[0] = avg4(pb[0], pb[1], pb[hw], pb[hw+1]);
                pa[1] = avg4(pb[1], pb[2], pb[hw+1], pb[hw+2]);
                pa[2] = avg4(pb[2], pb[3], pb[hw+2], pb[hw+3]);
                pa[3] = avg4(pb[3], pb[4], pb[hw+3], pb[hw+4]);
                break;
            }
            pa += ri->c_stride;
            pb += ri->c_stride;
        }

        pa = ri->current_frame->data[2] + (y * ri->y_stride)/4 + x/2;
        pb = ri->last_frame->data[2] + (my/2) * (ri->y_stride/2) + (mx + 1)/2;
    }
}
