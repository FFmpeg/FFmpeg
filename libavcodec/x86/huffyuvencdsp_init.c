/*
 * SIMD-optimized HuffYUV encoding functions
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * MMX optimization by Nick Kurshev <nickols_k@mail.ru>
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
#include "libavutil/pixdesc.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/huffyuvencdsp.h"

void ff_diff_int16_mmx (uint16_t *dst, const uint16_t *src1, const uint16_t *src2,
                        unsigned mask, int w);
void ff_diff_int16_sse2(uint16_t *dst, const uint16_t *src1, const uint16_t *src2,
                        unsigned mask, int w);
void ff_diff_int16_avx2(uint16_t *dst, const uint16_t *src1, const uint16_t *src2,
                        unsigned mask, int w);
void ff_sub_hfyu_median_pred_int16_mmxext(uint16_t *dst, const uint16_t *src1, const uint16_t *src2,
                                          unsigned mask, int w, int *left, int *left_top);

av_cold void ff_huffyuvencdsp_init_x86(HuffYUVEncDSPContext *c, AVCodecContext *avctx)
{
    av_unused int cpu_flags = av_get_cpu_flags();
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(avctx->pix_fmt);

    if (ARCH_X86_32 && EXTERNAL_MMX(cpu_flags)) {
        c->diff_int16 = ff_diff_int16_mmx;
    }

    if (EXTERNAL_MMXEXT(cpu_flags) && pix_desc && pix_desc->comp[0].depth<16) {
        c->sub_hfyu_median_pred_int16 = ff_sub_hfyu_median_pred_int16_mmxext;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->diff_int16 = ff_diff_int16_sse2;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->diff_int16 = ff_diff_int16_avx2;
    }
}
