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

void ff_rv40dsp_init_x86(RV34DSPContext *c, DSPContext *dsp)
{
    av_unused int mm_flags = av_get_cpu_flags();

#if HAVE_YASM
    if (mm_flags & AV_CPU_FLAG_MMX) {
        c->put_chroma_pixels_tab[0] = ff_put_rv40_chroma_mc8_mmx;
        c->put_chroma_pixels_tab[1] = ff_put_rv40_chroma_mc4_mmx;
    }
    if (mm_flags & AV_CPU_FLAG_MMX2) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_mmx2;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_mmx2;
    } else if (mm_flags & AV_CPU_FLAG_3DNOW) {
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_3dnow;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_3dnow;
    }
#endif
}
