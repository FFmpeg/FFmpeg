/*
 * Copyright (c) 2006 Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (c) 2010 Mans Rullgard <mans@mansr.com>
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

#include <stdint.h>
#include "avcodec.h"
#include "vp56dsp.h"

/* Gives very similar result than the vp6 version except in a few cases */
static int vp5_adjust(int v, int t)
{
    int s2, s1 = v >> 31;
    v ^= s1;
    v -= s1;
    v *= v < 2*t;
    v -= t;
    s2 = v >> 31;
    v ^= s2;
    v -= s2;
    v = t - v;
    v += s1;
    v ^= s1;
    return v;
}

static int vp6_adjust(int v, int t)
{
    int V = v, s = v >> 31;
    V ^= s;
    V -= s;
    if (V-t-1 >= (unsigned)(t-1))
        return v;
    V = 2*t - V;
    V += s;
    V ^= s;
    return V;
}


#define VP56_EDGE_FILTER(pfx, suf, pix_inc, line_inc)                   \
static void pfx##_edge_filter_##suf(uint8_t *yuv, int stride, int t)    \
{                                                                       \
    int pix2_inc = 2 * pix_inc;                                         \
    int i, v;                                                           \
                                                                        \
    for (i=0; i<12; i++) {                                              \
        v = (yuv[-pix2_inc] + 3*(yuv[0]-yuv[-pix_inc]) - yuv[pix_inc] + 4)>>3;\
        v = pfx##_adjust(v, t);                                         \
        yuv[-pix_inc] = av_clip_uint8(yuv[-pix_inc] + v);               \
        yuv[0] = av_clip_uint8(yuv[0] - v);                             \
        yuv += line_inc;                                                \
    }                                                                   \
}

VP56_EDGE_FILTER(vp5, hor, 1, stride)
VP56_EDGE_FILTER(vp5, ver, stride, 1)
VP56_EDGE_FILTER(vp6, hor, 1, stride)
VP56_EDGE_FILTER(vp6, ver, stride, 1)

void ff_vp56dsp_init(VP56DSPContext *s, enum CodecID codec)
{
    if (codec == CODEC_ID_VP5) {
        s->edge_filter_hor = vp5_edge_filter_hor;
        s->edge_filter_ver = vp5_edge_filter_ver;
    } else {
        s->edge_filter_hor = vp6_edge_filter_hor;
        s->edge_filter_ver = vp6_edge_filter_ver;
    }

    if (ARCH_ARM) ff_vp56dsp_init_arm(s, codec);
}
