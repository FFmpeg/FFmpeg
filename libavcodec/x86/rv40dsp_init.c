/*
 * RV40 decoder motion compensation functions x86-optimised
 * Copyright (c) 2008 Konstantin Shishkov
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * RV40 decoder motion compensation functions x86-optimised
 */

#include "libavcodec/rv34dsp.h"

void ff_put_rv40_chroma_mc8_mmx  (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc8_mmx2 (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc8_3dnow(uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);

void ff_put_rv40_chroma_mc4_mmx  (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc4_mmx2 (uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);
void ff_avg_rv40_chroma_mc4_3dnow(uint8_t *dst, uint8_t *src,
                                  int stride, int h, int x, int y);

#define DECLARE_WEIGHT(opt) \
void ff_rv40_weight_func_16_##opt(uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                  int w1, int w2, ptrdiff_t stride); \
void ff_rv40_weight_func_8_##opt (uint8_t *dst, uint8_t *src1, uint8_t *src2, \
                                  int w1, int w2, ptrdiff_t stride);
DECLARE_WEIGHT(mmx)
DECLARE_WEIGHT(sse2)
DECLARE_WEIGHT(ssse3)

void ff_rv40dsp_init_x86(RV34DSPContext *c, DSPContext *dsp)
{
#if HAVE_YASM
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_MMX) {
        c->put_chroma_pixels_tab[0] = ff_put_rv40_chroma_mc8_mmx;
        c->put_chroma_pixels_tab[1] = ff_put_rv40_chroma_mc4_mmx;
        c->rv40_weight_pixels_tab[0] = ff_rv40_weight_func_16_mmx;
        c->rv40_weight_pixels_tab[1] = ff_rv40_weight_func_8_mmx;
    }
    if (mm_flags & AV_CPU_FLAG_MMX2) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_mmx2;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_mmx2;
    } else if (mm_flags & AV_CPU_FLAG_3DNOW) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_3dnow;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_3dnow;
    }
    if (mm_flags & AV_CPU_FLAG_SSE2) {
        c->rv40_weight_pixels_tab[0] = ff_rv40_weight_func_16_sse2;
        c->rv40_weight_pixels_tab[1] = ff_rv40_weight_func_8_sse2;
    }
    if (mm_flags & AV_CPU_FLAG_SSSE3) {
        c->rv40_weight_pixels_tab[0] = ff_rv40_weight_func_16_ssse3;
        c->rv40_weight_pixels_tab[1] = ff_rv40_weight_func_8_ssse3;
    }
#endif
}
