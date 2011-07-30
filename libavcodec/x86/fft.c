/*
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
#include "libavcodec/dsputil.h"
#include "libavcodec/dct.h"
#include "fft.h"

av_cold void ff_fft_init_mmx(FFTContext *s)
{
#if HAVE_YASM
    int has_vectors = av_get_cpu_flags();
    if (has_vectors & AV_CPU_FLAG_AVX && HAVE_AVX && s->nbits >= 5) {
        /* AVX for SB */
        s->imdct_calc      = ff_imdct_calc_sse;
        s->imdct_half      = ff_imdct_half_avx;
        s->fft_permute     = ff_fft_permute_sse;
        s->fft_calc        = ff_fft_calc_avx;
        s->fft_permutation = FF_FFT_PERM_AVX;
    } else if (has_vectors & AV_CPU_FLAG_SSE && HAVE_SSE) {
        /* SSE for P3/P4/K8 */
        s->imdct_calc  = ff_imdct_calc_sse;
        s->imdct_half  = ff_imdct_half_sse;
        s->fft_permute = ff_fft_permute_sse;
        s->fft_calc    = ff_fft_calc_sse;
        s->fft_permutation = FF_FFT_PERM_SWAP_LSBS;
    } else if (has_vectors & AV_CPU_FLAG_3DNOWEXT && HAVE_AMD3DNOWEXT) {
        /* 3DNowEx for K7 */
        s->imdct_calc = ff_imdct_calc_3dn2;
        s->imdct_half = ff_imdct_half_3dn2;
        s->fft_calc   = ff_fft_calc_3dn2;
    } else if (has_vectors & AV_CPU_FLAG_3DNOW && HAVE_AMD3DNOW) {
        /* 3DNow! for K6-2/3 */
        s->imdct_calc = ff_imdct_calc_3dn;
        s->imdct_half = ff_imdct_half_3dn;
        s->fft_calc   = ff_fft_calc_3dn;
    }
#endif
}

#if CONFIG_DCT
av_cold void ff_dct_init_mmx(DCTContext *s)
{
#if HAVE_YASM
    int has_vectors = av_get_cpu_flags();
    if (has_vectors & AV_CPU_FLAG_AVX && HAVE_AVX)
        s->dct32 = ff_dct32_float_avx;
    else if (has_vectors & AV_CPU_FLAG_SSE2 && HAVE_SSE)
        s->dct32 = ff_dct32_float_sse2;
    else if (has_vectors & AV_CPU_FLAG_SSE && HAVE_SSE)
        s->dct32 = ff_dct32_float_sse;
#endif
}
#endif

