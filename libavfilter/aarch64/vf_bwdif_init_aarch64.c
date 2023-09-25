/*
 * bwdif aarch64 NEON optimisations
 *
 * Copyright (c) 2023 John Cox <jc@kynesim.co.uk>
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

#include "libavutil/common.h"
#include "libavfilter/bwdifdsp.h"
#include "libavutil/aarch64/cpu.h"

void ff_bwdif_filter_edge_neon(void *dst1, const void *prev1, const void *cur1, const void *next1,
                               int w, int prefs, int mrefs, int prefs2, int mrefs2,
                               int parity, int clip_max, int spat);

void ff_bwdif_filter_intra_neon(void *dst1, const void *cur1, int w, int prefs, int mrefs,
                                int prefs3, int mrefs3, int parity, int clip_max);

void ff_bwdif_filter_line_neon(void *dst1, const void *prev1, const void *cur1, const void *next1,
                               int w, int prefs, int mrefs, int prefs2, int mrefs2,
                               int prefs3, int mrefs3, int prefs4, int mrefs4,
                               int parity, int clip_max);

void ff_bwdif_filter_line3_neon(void * dst1, int d_stride,
                                const void * prev1, const void * cur1, const void * next1, int s_stride,
                                int w, int parity, int clip_max);


static void filter_line3_helper(void * dst1, int d_stride,
                                const void * prev1, const void * cur1, const void * next1, int s_stride,
                                int w, int parity, int clip_max)
{
    // Asm works on 16 byte chunks
    // If w is a multiple of 16 then all is good - if not then if width rounded
    // up to nearest 16 will fit in both src & dst strides then allow the asm
    // to write over the padding bytes as that is almost certainly faster than
    // having to invoke the C version to clean up the tail.
    const int w1 = FFALIGN(w, 16);
    const int w0 = clip_max != 255 ? 0 :
                   d_stride <= w1 && s_stride <= w1 ? w : w & ~15;

    ff_bwdif_filter_line3_neon(dst1, d_stride,
                               prev1, cur1, next1, s_stride,
                               w0, parity, clip_max);

    if (w0 < w)
        ff_bwdif_filter_line3_c((char *)dst1 + w0, d_stride,
                                (const char *)prev1 + w0, (const char *)cur1 + w0, (const char *)next1 + w0, s_stride,
                                w - w0, parity, clip_max);
}

static void filter_line_helper(void *dst1, const void *prev1, const void *cur1, const void *next1,
                               int w, int prefs, int mrefs, int prefs2, int mrefs2,
                               int prefs3, int mrefs3, int prefs4, int mrefs4,
                               int parity, int clip_max)
{
    const int w0 = clip_max != 255 ? 0 : w & ~15;

    ff_bwdif_filter_line_neon(dst1, prev1, cur1, next1,
                              w0, prefs, mrefs, prefs2, mrefs2, prefs3, mrefs3, prefs4, mrefs4, parity, clip_max);

    if (w0 < w)
        ff_bwdif_filter_line_c((char *)dst1 + w0, (char *)prev1 + w0, (char *)cur1 + w0, (char *)next1 + w0,
                               w - w0, prefs, mrefs, prefs2, mrefs2, prefs3, mrefs3, prefs4, mrefs4, parity, clip_max);
}

static void filter_edge_helper(void *dst1, const void *prev1, const void *cur1, const void *next1,
                               int w, int prefs, int mrefs, int prefs2, int mrefs2,
                               int parity, int clip_max, int spat)
{
    const int w0 = clip_max != 255 ? 0 : w & ~15;

    ff_bwdif_filter_edge_neon(dst1, prev1, cur1, next1, w0, prefs, mrefs, prefs2, mrefs2,
                              parity, clip_max, spat);

    if (w0 < w)
        ff_bwdif_filter_edge_c((char *)dst1 + w0, (char *)prev1 + w0, (char *)cur1 + w0, (char *)next1 + w0,
                               w - w0, prefs, mrefs, prefs2, mrefs2,
                               parity, clip_max, spat);
}

static void filter_intra_helper(void *dst1, const void *cur1, int w, int prefs, int mrefs,
                                int prefs3, int mrefs3, int parity, int clip_max)
{
    const int w0 = clip_max != 255 ? 0 : w & ~15;

    ff_bwdif_filter_intra_neon(dst1, cur1, w0, prefs, mrefs, prefs3, mrefs3, parity, clip_max);

    if (w0 < w)
        ff_bwdif_filter_intra_c((char *)dst1 + w0, (char *)cur1 + w0,
                                w - w0, prefs, mrefs, prefs3, mrefs3, parity, clip_max);
}

void
ff_bwdif_init_aarch64(BWDIFDSPContext *s, int bit_depth)
{
    const int cpu_flags = av_get_cpu_flags();

    if (bit_depth != 8)
        return;

    if (!have_neon(cpu_flags))
        return;

    s->filter_intra = filter_intra_helper;
    s->filter_line  = filter_line_helper;
    s->filter_edge  = filter_edge_helper;
    s->filter_line3 = filter_line3_helper;
}

