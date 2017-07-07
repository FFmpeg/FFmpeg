/*
 * SIMD optimized non-power-of-two MDCT functions
 *
 * Copyright (C) 2017 Rostislav Pehlivanov <atomnuker@gmail.com>
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

#include "libavutil/x86/cpu.h"
#include "libavcodec/mdct15.h"

void ff_fft15_avx(FFTComplex *out, FFTComplex *in, FFTComplex *exptab, ptrdiff_t stride);

static void perm_twiddles(MDCT15Context *s)
{
    int k;
    FFTComplex exp_5point[4];

    FFTComplex tmp[21], tmp2[30];
    memcpy(tmp, s->exptab, sizeof(FFTComplex)*21);

    /* 15-point FFT twiddles */
    for (k = 0; k < 5; k++) {
        tmp2[6*k + 0] = tmp[k +  0];
        tmp2[6*k + 2] = tmp[k +  5];
        tmp2[6*k + 4] = tmp[k + 10];

        tmp2[6*k + 1] = tmp[2 * (k + 0)];
        tmp2[6*k + 3] = tmp[2 * (k + 5)];
        tmp2[6*k + 5] = tmp[2 *  k + 5 ];
    }

    for (k = 0; k < 6; k++) {
        FFTComplex ac_exp[] = {
            { tmp2[6*1 + k].re,  tmp2[6*1 + k].re },
            { tmp2[6*2 + k].re,  tmp2[6*2 + k].re },
            { tmp2[6*3 + k].re,  tmp2[6*3 + k].re },
            { tmp2[6*4 + k].re,  tmp2[6*4 + k].re },
            { tmp2[6*1 + k].im, -tmp2[6*1 + k].im },
            { tmp2[6*2 + k].im, -tmp2[6*2 + k].im },
            { tmp2[6*3 + k].im, -tmp2[6*3 + k].im },
            { tmp2[6*4 + k].im, -tmp2[6*4 + k].im },
        };
        memcpy(s->exptab + 8*k, ac_exp, 8*sizeof(FFTComplex));
    }

    /* Specialcase when k = 0 */
    for (k = 0; k < 3; k++) {
        FFTComplex dc_exp[] = {
            { tmp2[2*k + 0].re, -tmp2[2*k + 0].im },
            { tmp2[2*k + 0].im,  tmp2[2*k + 0].re },
            { tmp2[2*k + 1].re, -tmp2[2*k + 1].im },
            { tmp2[2*k + 1].im,  tmp2[2*k + 1].re },
        };
        memcpy(s->exptab + 8*6 + 4*k, dc_exp, 4*sizeof(FFTComplex));
    }

    /* 5-point FFT twiddles */
    exp_5point[0].re = exp_5point[0].im = tmp[19].re;
    exp_5point[1].re = exp_5point[1].im = tmp[19].im;
    exp_5point[2].re = exp_5point[2].im = tmp[20].re;
    exp_5point[3].re = exp_5point[3].im = tmp[20].im;

    memcpy(s->exptab + 8*6 + 4*3, exp_5point, 4*sizeof(FFTComplex));
}

av_cold void ff_mdct15_init_x86(MDCT15Context *s)
{
    int adjust_twiddles = 0;
    int cpu_flags = av_get_cpu_flags();

    if (ARCH_X86_64 && EXTERNAL_AVX(cpu_flags)) {
        s->fft15 = ff_fft15_avx;
        adjust_twiddles = 1;
    }

    if (adjust_twiddles)
        perm_twiddles(s);
}
