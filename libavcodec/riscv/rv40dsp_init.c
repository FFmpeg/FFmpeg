/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/rv34dsp.h"

void ff_put_rv40_chroma_mc8_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int h, int x, int y);
void ff_put_rv40_chroma_mc4_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int h, int x, int y);

void ff_avg_rv40_chroma_mc8_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int h, int x, int y);
void ff_avg_rv40_chroma_mc4_rvv(uint8_t *dst, const uint8_t *src, ptrdiff_t stride,
                                 int h, int x, int y);

av_cold void ff_rv40dsp_init_riscv(RV34DSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && ff_rv_vlen_least(128) &&
        (flags & AV_CPU_FLAG_RVB)) {
        c->put_chroma_pixels_tab[0] = ff_put_rv40_chroma_mc8_rvv;
        c->put_chroma_pixels_tab[1] = ff_put_rv40_chroma_mc4_rvv;
        c->avg_chroma_pixels_tab[0] = ff_avg_rv40_chroma_mc8_rvv;
        c->avg_chroma_pixels_tab[1] = ff_avg_rv40_chroma_mc4_rvv;
    }
#endif
}
