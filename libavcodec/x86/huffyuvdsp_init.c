/*
 * Copyright (c) 2009 Loren Merritt <lorenm@u.washington.edu>
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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/huffyuvdsp.h"

void ff_add_hfyu_left_pred_bgr32_mmx(uint8_t *dst, const uint8_t *src,
                                     intptr_t w, uint8_t *left);
void ff_add_hfyu_left_pred_bgr32_sse2(uint8_t *dst, const uint8_t *src,
                                      intptr_t w, uint8_t *left);

av_cold void ff_huffyuvdsp_init_x86(HuffYUVDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_32 && EXTERNAL_MMX(cpu_flags)) {
        c->add_hfyu_left_pred_bgr32 = ff_add_hfyu_left_pred_bgr32_mmx;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_hfyu_left_pred_bgr32 = ff_add_hfyu_left_pred_bgr32_sse2;
    }
}
