/*
 * BobWeaver Deinterlacing Filter DSP functions
 * Copyright (C) 2016 Thomas Mundt <loudmax@yahoo.de>
 *
 * Based on YADIF (Yet Another Deinterlacing Filter)
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * With use of Weston 3 Field Deinterlacing Filter algorithm
 * Copyright (C) 2012 British Broadcasting Corporation, All Rights Reserved
 * Author of de-interlace algorithm: Jim Easterbrook for BBC R&D
 * Based on the process described by Martin Weston for BBC R&D
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
#include <string.h>

#include "config.h"

#include "bwdifdsp.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/macros.h"

/*
 * Filter coefficients coef_lf and coef_hf taken from BBC PH-2071 (Weston 3 Field Deinterlacer).
 * Used when there is spatial and temporal interpolation.
 * Filter coefficients coef_sp are used when there is spatial interpolation only.
 * Adjusted for matching visual sharpness impression of spatial and temporal interpolation.
 */
static const uint16_t coef_lf[2] = { 4309, 213 };
static const uint16_t coef_hf[3] = { 5570, 3801, 1016 };
static const uint16_t coef_sp[2] = { 5077, 981 };


#define FILTER_INTRA() \
    for (x = 0; x < w; x++) { \
        interpol = (coef_sp[0] * (cur[mrefs] + cur[prefs]) - coef_sp[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
        dst[0] = av_clip(interpol, 0, clip_max); \
 \
        dst++; \
        cur++; \
    }

#define FILTER1() \
    for (x = 0; x < w; x++) { \
        int c = cur[mrefs]; \
        int d = (prev2[0] + next2[0]) >> 1; \
        int e = cur[prefs]; \
        int temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        int temporal_diff1 =(FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e)) >> 1; \
        int temporal_diff2 =(FFABS(next[mrefs] - c) + FFABS(next[prefs] - e)) >> 1; \
        int diff = FFMAX3(temporal_diff0 >> 1, temporal_diff1, temporal_diff2); \
 \
        if (!diff) { \
            dst[0] = d; \
        } else {

#define SPAT_CHECK() \
            int b = ((prev2[mrefs2] + next2[mrefs2]) >> 1) - c; \
            int f = ((prev2[prefs2] + next2[prefs2]) >> 1) - e; \
            int dc = d - c; \
            int de = d - e; \
            int max = FFMAX3(de, dc, FFMIN(b, f)); \
            int min = FFMIN3(de, dc, FFMAX(b, f)); \
            diff = FFMAX3(diff, min, -max);

#define FILTER_LINE() \
            SPAT_CHECK() \
            if (FFABS(c - e) > temporal_diff0) { \
                interpol = (((coef_hf[0] * (prev2[0] + next2[0]) \
                    - coef_hf[1] * (prev2[mrefs2] + next2[mrefs2] + prev2[prefs2] + next2[prefs2]) \
                    + coef_hf[2] * (prev2[mrefs4] + next2[mrefs4] + prev2[prefs4] + next2[prefs4])) >> 2) \
                    + coef_lf[0] * (c + e) - coef_lf[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
            } else { \
                interpol = (coef_sp[0] * (c + e) - coef_sp[1] * (cur[mrefs3] + cur[prefs3])) >> 13; \
            }

#define FILTER_EDGE() \
            if (spat) { \
                SPAT_CHECK() \
            } \
            interpol = (c + e) >> 1;

#define FILTER2() \
            if (interpol > d + diff) \
                interpol = d + diff; \
            else if (interpol < d - diff) \
                interpol = d - diff; \
 \
            dst[0] = av_clip(interpol, 0, clip_max); \
        } \
 \
        dst++; \
        cur++; \
        prev++; \
        next++; \
        prev2++; \
        next2++; \
    }

void ff_bwdif_filter_intra_c(void *dst1, const void *cur1, int w, int prefs, int mrefs,
                             int prefs3, int mrefs3, int parity, int clip_max)
{
    uint8_t *dst = dst1;
    const uint8_t *cur = cur1;
    int interpol, x;

    FILTER_INTRA()
}

void ff_bwdif_filter_line_c(void *dst1, const void *prev1, const void *cur1, const void *next1,
                            int w, int prefs, int mrefs, int prefs2, int mrefs2,
                            int prefs3, int mrefs3, int prefs4, int mrefs4,
                            int parity, int clip_max)
{
    uint8_t *dst   = dst1;
    const uint8_t *prev  = prev1;
    const uint8_t *cur   = cur1;
    const uint8_t *next  = next1;
    const uint8_t *prev2 = parity ? prev : cur ;
    const uint8_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_LINE()
    FILTER2()
}

void ff_bwdif_filter_edge_c(void *dst1, const void *prev1, const void *cur1, const void *next1,
                            int w, int prefs, int mrefs, int prefs2, int mrefs2,
                            int parity, int clip_max, int spat)
{
    uint8_t *dst   = dst1;
    const uint8_t *prev  = prev1;
    const uint8_t *cur   = cur1;
    const uint8_t *next  = next1;
    const uint8_t *prev2 = parity ? prev : cur ;
    const uint8_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_EDGE()
    FILTER2()
}

static void filter_intra_16bit(void *dst1, const void *cur1, int w, int prefs, int mrefs,
                               int prefs3, int mrefs3, int parity, int clip_max)
{
    uint16_t *dst = dst1;
    const uint16_t *cur = cur1;
    int interpol, x;

    FILTER_INTRA()
}

static void filter_line_c_16bit(void *dst1, const void *prev1, const void *cur1, const void *next1,
                                int w, int prefs, int mrefs, int prefs2, int mrefs2,
                                int prefs3, int mrefs3, int prefs4, int mrefs4,
                                int parity, int clip_max)
{
    uint16_t *dst   = dst1;
    const uint16_t *prev  = prev1;
    const uint16_t *cur   = cur1;
    const uint16_t *next  = next1;
    const uint16_t *prev2 = parity ? prev : cur ;
    const uint16_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_LINE()
    FILTER2()
}

static void filter_edge_16bit(void *dst1, const void *prev1, const void *cur1, const void *next1,
                              int w, int prefs, int mrefs, int prefs2, int mrefs2,
                              int parity, int clip_max, int spat)
{
    uint16_t *dst   = dst1;
    const uint16_t *prev  = prev1;
    const uint16_t *cur   = cur1;
    const uint16_t *next  = next1;
    const uint16_t *prev2 = parity ? prev : cur ;
    const uint16_t *next2 = parity ? cur  : next;
    int interpol, x;

    FILTER1()
    FILTER_EDGE()
    FILTER2()
}

av_cold void ff_bwdif_init_filter_line(BWDIFDSPContext *s, int bit_depth)
{
    s->filter_line3 = 0;
    if (bit_depth > 8) {
        s->filter_intra = filter_intra_16bit;
        s->filter_line  = filter_line_c_16bit;
        s->filter_edge  = filter_edge_16bit;
    } else {
        s->filter_intra = ff_bwdif_filter_intra_c;
        s->filter_line  = ff_bwdif_filter_line_c;
        s->filter_edge  = ff_bwdif_filter_edge_c;
    }

#if ARCH_X86
    ff_bwdif_init_x86(s, bit_depth);
#elif ARCH_AARCH64
    ff_bwdif_init_aarch64(s, bit_depth);
#endif
}
