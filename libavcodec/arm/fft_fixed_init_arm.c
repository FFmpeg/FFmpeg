/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#define CONFIG_FFT_FLOAT 0
#include "libavcodec/fft.h"

void ff_fft_fixed_calc_neon(FFTContext *s, FFTComplex *z);
void ff_mdct_fixed_calc_neon(FFTContext *s, FFTSample *o, const FFTSample *i);
void ff_mdct_fixed_calcw_neon(FFTContext *s, FFTDouble *o, const FFTSample *i);

av_cold void ff_fft_fixed_init_arm(FFTContext *s)
{
    if (HAVE_NEON) {
        s->fft_permutation = FF_FFT_PERM_SWAP_LSBS;
        s->fft_calc        = ff_fft_fixed_calc_neon;

#if CONFIG_MDCT
        if (!s->inverse && s->mdct_bits >= 5) {
            s->mdct_permutation = FF_MDCT_PERM_INTERLEAVE;
            s->mdct_calc        = ff_mdct_fixed_calc_neon;
            s->mdct_calcw       = ff_mdct_fixed_calcw_neon;
        }
#endif
    }
}
