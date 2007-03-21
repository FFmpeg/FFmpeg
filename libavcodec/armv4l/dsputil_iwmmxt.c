/*
 * iWMMXt optimized DSP utils
 * Copyright (c) 2004 AGAWA Koji
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

#include "../dsputil.h"

#define DEF(x, y) x ## _no_rnd_ ## y ##_iwmmxt
#define SET_RND(regd)  __asm__ __volatile__ ("mov r12, #1 \n\t tbcsth " #regd ", r12":::"r12");
#define WAVG2B "wavg2b"
#include "dsputil_iwmmxt_rnd.h"
#undef DEF
#undef SET_RND
#undef WAVG2B

#define DEF(x, y) x ## _ ## y ##_iwmmxt
#define SET_RND(regd)  __asm__ __volatile__ ("mov r12, #2 \n\t tbcsth " #regd ", r12":::"r12");
#define WAVG2B "wavg2br"
#include "dsputil_iwmmxt_rnd.h"
#undef DEF
#undef SET_RND
#undef WAVG2BR

// need scheduling
#define OP(AVG)                                         \
    asm volatile (                                      \
        /* alignment */                                 \
        "and r12, %[pixels], #7 \n\t"                   \
        "bic %[pixels], %[pixels], #7 \n\t"             \
        "tmcr wcgr1, r12 \n\t"                          \
                                                        \
        "wldrd wr0, [%[pixels]] \n\t"                   \
        "wldrd wr1, [%[pixels], #8] \n\t"               \
        "add %[pixels], %[pixels], %[line_size] \n\t"   \
        "walignr1 wr4, wr0, wr1 \n\t"                   \
                                                        \
        "1: \n\t"                                       \
                                                        \
        "wldrd wr2, [%[pixels]] \n\t"                   \
        "wldrd wr3, [%[pixels], #8] \n\t"               \
        "add %[pixels], %[pixels], %[line_size] \n\t"   \
        "pld [%[pixels]] \n\t"                          \
        "walignr1 wr5, wr2, wr3 \n\t"                   \
        AVG " wr6, wr4, wr5 \n\t"                       \
        "wstrd wr6, [%[block]] \n\t"                    \
        "add %[block], %[block], %[line_size] \n\t"     \
                                                        \
        "wldrd wr0, [%[pixels]] \n\t"                   \
        "wldrd wr1, [%[pixels], #8] \n\t"               \
        "add %[pixels], %[pixels], %[line_size] \n\t"   \
        "walignr1 wr4, wr0, wr1 \n\t"                   \
        "pld [%[pixels]] \n\t"                          \
        AVG " wr6, wr4, wr5 \n\t"                       \
        "wstrd wr6, [%[block]] \n\t"                    \
        "add %[block], %[block], %[line_size] \n\t"     \
                                                        \
        "subs %[h], %[h], #2 \n\t"                      \
        "bne 1b \n\t"                                   \
        : [block]"+r"(block), [pixels]"+r"(pixels), [h]"+r"(h)  \
        : [line_size]"r"(line_size) \
        : "memory", "r12");
void put_pixels8_y2_iwmmxt(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    OP("wavg2br");
}
void put_no_rnd_pixels8_y2_iwmmxt(uint8_t *block, const uint8_t *pixels, const int line_size, int h)
{
    OP("wavg2b");
}
#undef OP

void add_pixels_clamped_iwmmxt(const DCTELEM *block, uint8_t *pixels, int line_size)
{
    uint8_t *pixels2 = pixels + line_size;

    __asm__ __volatile__ (
        "mov            r12, #4                 \n\t"
        "1:                                     \n\t"
        "pld            [%[pixels], %[line_size2]]              \n\t"
        "pld            [%[pixels2], %[line_size2]]             \n\t"
        "wldrd          wr4, [%[pixels]]        \n\t"
        "wldrd          wr5, [%[pixels2]]       \n\t"
        "pld            [%[block], #32]         \n\t"
        "wunpckelub     wr6, wr4                \n\t"
        "wldrd          wr0, [%[block]]         \n\t"
        "wunpckehub     wr7, wr4                \n\t"
        "wldrd          wr1, [%[block], #8]     \n\t"
        "wunpckelub     wr8, wr5                \n\t"
        "wldrd          wr2, [%[block], #16]    \n\t"
        "wunpckehub     wr9, wr5                \n\t"
        "wldrd          wr3, [%[block], #24]    \n\t"
        "add            %[block], %[block], #32 \n\t"
        "waddhss        wr10, wr0, wr6          \n\t"
        "waddhss        wr11, wr1, wr7          \n\t"
        "waddhss        wr12, wr2, wr8          \n\t"
        "waddhss        wr13, wr3, wr9          \n\t"
        "wpackhus       wr14, wr10, wr11        \n\t"
        "wpackhus       wr15, wr12, wr13        \n\t"
        "wstrd          wr14, [%[pixels]]       \n\t"
        "add            %[pixels], %[pixels], %[line_size2]     \n\t"
        "subs           r12, r12, #1            \n\t"
        "wstrd          wr15, [%[pixels2]]      \n\t"
        "add            %[pixels2], %[pixels2], %[line_size2]   \n\t"
        "bne            1b                      \n\t"
        : [block]"+r"(block), [pixels]"+r"(pixels), [pixels2]"+r"(pixels2)
        : [line_size2]"r"(line_size << 1)
        : "cc", "memory", "r12");
}

static void nop(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    return;
}

/* A run time test is not simple. If this file is compiled in
 * then we should install the functions
 */
int mm_flags = MM_IWMMXT; /* multimedia extension flags */

void dsputil_init_iwmmxt(DSPContext* c, AVCodecContext *avctx)
{
    if (avctx->dsp_mask) {
        if (avctx->dsp_mask & FF_MM_FORCE)
            mm_flags |= (avctx->dsp_mask & 0xffff);
        else
            mm_flags &= ~(avctx->dsp_mask & 0xffff);
    }

    if (!(mm_flags & MM_IWMMXT)) return;

    c->add_pixels_clamped = add_pixels_clamped_iwmmxt;

    c->put_pixels_tab[0][0] = put_pixels16_iwmmxt;
    c->put_pixels_tab[0][1] = put_pixels16_x2_iwmmxt;
    c->put_pixels_tab[0][2] = put_pixels16_y2_iwmmxt;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_iwmmxt;
    c->put_no_rnd_pixels_tab[0][0] = put_pixels16_iwmmxt;
    c->put_no_rnd_pixels_tab[0][1] = put_no_rnd_pixels16_x2_iwmmxt;
    c->put_no_rnd_pixels_tab[0][2] = put_no_rnd_pixels16_y2_iwmmxt;
    c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_iwmmxt;

    c->put_pixels_tab[1][0] = put_pixels8_iwmmxt;
    c->put_pixels_tab[1][1] = put_pixels8_x2_iwmmxt;
    c->put_pixels_tab[1][2] = put_pixels8_y2_iwmmxt;
    c->put_pixels_tab[1][3] = put_pixels8_xy2_iwmmxt;
    c->put_no_rnd_pixels_tab[1][0] = put_pixels8_iwmmxt;
    c->put_no_rnd_pixels_tab[1][1] = put_no_rnd_pixels8_x2_iwmmxt;
    c->put_no_rnd_pixels_tab[1][2] = put_no_rnd_pixels8_y2_iwmmxt;
    c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_iwmmxt;

    c->avg_pixels_tab[0][0] = avg_pixels16_iwmmxt;
    c->avg_pixels_tab[0][1] = avg_pixels16_x2_iwmmxt;
    c->avg_pixels_tab[0][2] = avg_pixels16_y2_iwmmxt;
    c->avg_pixels_tab[0][3] = avg_pixels16_xy2_iwmmxt;
    c->avg_no_rnd_pixels_tab[0][0] = avg_pixels16_iwmmxt;
    c->avg_no_rnd_pixels_tab[0][1] = avg_no_rnd_pixels16_x2_iwmmxt;
    c->avg_no_rnd_pixels_tab[0][2] = avg_no_rnd_pixels16_y2_iwmmxt;
    c->avg_no_rnd_pixels_tab[0][3] = avg_no_rnd_pixels16_xy2_iwmmxt;

    c->avg_pixels_tab[1][0] = avg_pixels8_iwmmxt;
    c->avg_pixels_tab[1][1] = avg_pixels8_x2_iwmmxt;
    c->avg_pixels_tab[1][2] = avg_pixels8_y2_iwmmxt;
    c->avg_pixels_tab[1][3] = avg_pixels8_xy2_iwmmxt;
    c->avg_no_rnd_pixels_tab[1][0] = avg_no_rnd_pixels8_iwmmxt;
    c->avg_no_rnd_pixels_tab[1][1] = avg_no_rnd_pixels8_x2_iwmmxt;
    c->avg_no_rnd_pixels_tab[1][2] = avg_no_rnd_pixels8_y2_iwmmxt;
    c->avg_no_rnd_pixels_tab[1][3] = avg_no_rnd_pixels8_xy2_iwmmxt;
}
