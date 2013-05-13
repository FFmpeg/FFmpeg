/*
 * RV30/40 MMX/SSE2 optimizations
 * Copyright (C) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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

#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/rv34dsp.h"

void ff_rv34_idct_dc_mmxext(int16_t *block);
void ff_rv34_idct_dc_noround_mmxext(int16_t *block);
void ff_rv34_idct_dc_add_mmx(uint8_t *dst, ptrdiff_t stride, int dc);
void ff_rv34_idct_dc_add_sse4(uint8_t *dst, ptrdiff_t stride, int dc);
void ff_rv34_idct_add_mmxext(uint8_t *dst, ptrdiff_t stride, int16_t *block);

av_cold void ff_rv34dsp_init_x86(RV34DSPContext* c)
{
    int mm_flags = av_get_cpu_flags();

    if (EXTERNAL_MMX(mm_flags))
        c->rv34_idct_dc_add = ff_rv34_idct_dc_add_mmx;
    if (EXTERNAL_MMXEXT(mm_flags)) {
        c->rv34_inv_transform_dc = ff_rv34_idct_dc_noround_mmxext;
        c->rv34_idct_add         = ff_rv34_idct_add_mmxext;
    }
    if (EXTERNAL_SSE4(mm_flags))
        c->rv34_idct_dc_add = ff_rv34_idct_dc_add_sse4;
}
