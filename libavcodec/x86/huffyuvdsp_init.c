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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/huffyuvdsp.h"
#include "huffyuvdsp.h"

void ff_add_bytes_mmx(uint8_t *dst, uint8_t *src, intptr_t w);
void ff_add_bytes_sse2(uint8_t *dst, uint8_t *src, intptr_t w);

void ff_add_hfyu_median_pred_mmxext(uint8_t *dst, const uint8_t *top,
                                    const uint8_t *diff, intptr_t w,
                                    int *left, int *left_top);
void ff_add_hfyu_median_pred_sse2(uint8_t *dst, const uint8_t *top,
                                  const uint8_t *diff, intptr_t w,
                                  int *left, int *left_top);

int  ff_add_hfyu_left_pred_ssse3(uint8_t *dst, const uint8_t *src,
                                 intptr_t w, int left);
int  ff_add_hfyu_left_pred_sse4(uint8_t *dst, const uint8_t *src,
                                intptr_t w, int left);

void ff_add_hfyu_left_pred_bgr32_mmx(uint8_t *dst, const uint8_t *src,
                                     intptr_t w, uint8_t *left);
void ff_add_hfyu_left_pred_bgr32_sse2(uint8_t *dst, const uint8_t *src,
                                      intptr_t w, uint8_t *left);

av_cold void ff_huffyuvdsp_init_x86(HuffYUVDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

#if HAVE_7REGS && HAVE_INLINE_ASM
    if (ARCH_X86_32 && cpu_flags & AV_CPU_FLAG_CMOV)
        c->add_hfyu_median_pred = ff_add_hfyu_median_pred_cmov;
#endif

    if (ARCH_X86_32 && EXTERNAL_MMX(cpu_flags)) {
        c->add_bytes = ff_add_bytes_mmx;
        c->add_hfyu_left_pred_bgr32 = ff_add_hfyu_left_pred_bgr32_mmx;
    }

    if (ARCH_X86_32 && EXTERNAL_MMXEXT(cpu_flags)) {
        /* slower than cmov version on AMD */
        if (!(cpu_flags & AV_CPU_FLAG_3DNOW))
            c->add_hfyu_median_pred = ff_add_hfyu_median_pred_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->add_bytes            = ff_add_bytes_sse2;
        c->add_hfyu_median_pred = ff_add_hfyu_median_pred_sse2;
        c->add_hfyu_left_pred_bgr32 = ff_add_hfyu_left_pred_bgr32_sse2;
    }

    if (EXTERNAL_SSSE3(cpu_flags)) {
        c->add_hfyu_left_pred = ff_add_hfyu_left_pred_ssse3;
        if (cpu_flags & AV_CPU_FLAG_SSE4) // not really SSE4, just slow on Conroe
            c->add_hfyu_left_pred = ff_add_hfyu_left_pred_sse4;
    }
}
