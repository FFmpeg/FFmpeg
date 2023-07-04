/*
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

#ifndef AVFILTER_BWDIF_H
#define AVFILTER_BWDIF_H

#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "yadif.h"

typedef struct BWDIFContext {
    YADIFContext yadif;

    void (*filter_intra)(void *dst1, void *cur1, int w, int prefs, int mrefs,
                         int prefs3, int mrefs3, int parity, int clip_max);
    void (*filter_line)(void *dst, void *prev, void *cur, void *next,
                        int w, int prefs, int mrefs, int prefs2, int mrefs2,
                        int prefs3, int mrefs3, int prefs4, int mrefs4,
                        int parity, int clip_max);
    void (*filter_edge)(void *dst, void *prev, void *cur, void *next,
                        int w, int prefs, int mrefs, int prefs2, int mrefs2,
                        int parity, int clip_max, int spat);
    void (*filter_line3)(void *dst, int dstride,
                         const void *prev, const void *cur, const void *next, int prefs,
                         int w, int parity, int clip_max);
} BWDIFContext;

void ff_bwdif_init_filter_line(BWDIFContext *bwdif, int bit_depth);
void ff_bwdif_init_x86(BWDIFContext *bwdif, int bit_depth);
void ff_bwdif_init_aarch64(BWDIFContext *bwdif, int bit_depth);

void ff_bwdif_filter_edge_c(void *dst1, void *prev1, void *cur1, void *next1,
                            int w, int prefs, int mrefs, int prefs2, int mrefs2,
                            int parity, int clip_max, int spat);

void ff_bwdif_filter_intra_c(void *dst1, void *cur1, int w, int prefs, int mrefs,
                             int prefs3, int mrefs3, int parity, int clip_max);

void ff_bwdif_filter_line_c(void *dst1, void *prev1, void *cur1, void *next1,
                            int w, int prefs, int mrefs, int prefs2, int mrefs2,
                            int prefs3, int mrefs3, int prefs4, int mrefs4,
                            int parity, int clip_max);

void ff_bwdif_filter_line3_c(void * dst1, int d_stride,
                             const void * prev1, const void * cur1, const void * next1, int s_stride,
                             int w, int parity, int clip_max);

#endif /* AVFILTER_BWDIF_H */
