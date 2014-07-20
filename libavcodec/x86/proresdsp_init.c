/*
 * Apple ProRes compatible decoder
 *
 * Copyright (c) 2010-2011 Maxim Poliakovski
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
#include "libavutil/x86/cpu.h"
#include "libavcodec/idctdsp.h"
#include "libavcodec/proresdsp.h"

void ff_prores_idct_put_10_sse2(uint16_t *dst, int linesize,
                                int16_t *block, const int16_t *qmat);
void ff_prores_idct_put_10_sse4(uint16_t *dst, int linesize,
                                int16_t *block, const int16_t *qmat);
void ff_prores_idct_put_10_avx (uint16_t *dst, int linesize,
                                int16_t *block, const int16_t *qmat);

av_cold void ff_proresdsp_init_x86(ProresDSPContext *dsp, AVCodecContext *avctx)
{
#if ARCH_X86_64
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        dsp->idct_permutation_type = FF_IDCT_PERM_TRANSPOSE;
        dsp->idct_put = ff_prores_idct_put_10_sse2;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        dsp->idct_permutation_type = FF_IDCT_PERM_TRANSPOSE;
        dsp->idct_put = ff_prores_idct_put_10_sse4;
    }

    if (EXTERNAL_AVX(cpu_flags)) {
        dsp->idct_permutation_type = FF_IDCT_PERM_TRANSPOSE;
        dsp->idct_put = ff_prores_idct_put_10_avx;
    }
#endif /* ARCH_X86_64 */
}
