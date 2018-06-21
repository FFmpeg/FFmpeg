/*
 * Copyright (c) 2015 James Darnley
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/removegrain.h"

void ff_rg_fl_mode_1_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_10_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_11_12_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_13_14_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_19_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_20_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_21_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_22_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
#if ARCH_X86_64
void ff_rg_fl_mode_2_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_3_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_4_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_5_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_6_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_7_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_8_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_9_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_15_16_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_17_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_18_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_23_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
void ff_rg_fl_mode_24_sse2(uint8_t *dst, uint8_t *src, ptrdiff_t stride, int pixels);
#endif

av_cold void ff_removegrain_init_x86(RemoveGrainContext *rg)
{
#if CONFIG_GPL
    int cpu_flags = av_get_cpu_flags();
    int i;

    for (i = 0; i < rg->nb_planes; i++) {
        if (EXTERNAL_SSE2(cpu_flags))
            switch (rg->mode[i]) {
                case 1: rg->fl[i] = ff_rg_fl_mode_1_sse2; break;
                case 10: rg->fl[i] = ff_rg_fl_mode_10_sse2; break;
                case 11: /* fall through */
                case 12: rg->fl[i] = ff_rg_fl_mode_11_12_sse2; break;
                case 13: /* fall through */
                case 14: rg->fl[i] = ff_rg_fl_mode_13_14_sse2; break;
                case 19: rg->fl[i] = ff_rg_fl_mode_19_sse2; break;
                case 20: rg->fl[i] = ff_rg_fl_mode_20_sse2; break;
                case 21: rg->fl[i] = ff_rg_fl_mode_21_sse2; break;
                case 22: rg->fl[i] = ff_rg_fl_mode_22_sse2; break;
#if ARCH_X86_64
                case 2: rg->fl[i] = ff_rg_fl_mode_2_sse2; break;
                case 3: rg->fl[i] = ff_rg_fl_mode_3_sse2; break;
                case 4: rg->fl[i] = ff_rg_fl_mode_4_sse2; break;
                case 5: rg->fl[i] = ff_rg_fl_mode_5_sse2; break;
                case 6: rg->fl[i] = ff_rg_fl_mode_6_sse2; break;
                case 7: rg->fl[i] = ff_rg_fl_mode_7_sse2; break;
                case 8: rg->fl[i] = ff_rg_fl_mode_8_sse2; break;
                case 9: rg->fl[i] = ff_rg_fl_mode_9_sse2; break;
                case 15: /* fall through */
                case 16: rg->fl[i] = ff_rg_fl_mode_15_16_sse2; break;
                case 17: rg->fl[i] = ff_rg_fl_mode_17_sse2; break;
                case 18: rg->fl[i] = ff_rg_fl_mode_18_sse2; break;
                case 23: rg->fl[i] = ff_rg_fl_mode_23_sse2; break;
                case 24: rg->fl[i] = ff_rg_fl_mode_24_sse2; break;
#endif /* ARCH_x86_64 */
            }
    }
#endif /* CONFIG_GPL */
}
