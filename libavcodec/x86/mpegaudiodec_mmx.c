/*
 * MMX optimized MP3 decoding functions
 * Copyright (c) 2010 Vitor Sessak
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

#include "libavutil/cpu.h"
#include "libavutil/x86_cpu.h"

#define CONFIG_FLOAT 1
#include "libavcodec/mpegaudio.h"

#define MACS(rt, ra, rb) rt+=(ra)*(rb)
#define MLSS(rt, ra, rb) rt-=(ra)*(rb)

#define SUM8(op, sum, w, p)               \
{                                         \
    op(sum, (w)[0 * 64], (p)[0 * 64]);    \
    op(sum, (w)[1 * 64], (p)[1 * 64]);    \
    op(sum, (w)[2 * 64], (p)[2 * 64]);    \
    op(sum, (w)[3 * 64], (p)[3 * 64]);    \
    op(sum, (w)[4 * 64], (p)[4 * 64]);    \
    op(sum, (w)[5 * 64], (p)[5 * 64]);    \
    op(sum, (w)[6 * 64], (p)[6 * 64]);    \
    op(sum, (w)[7 * 64], (p)[7 * 64]);    \
}

static void apply_window(const float *buf, const float *win1,
                         const float *win2, float *sum1, float *sum2, int len)
{
    x86_reg count = - 4*len;
    const float *win1a = win1+len;
    const float *win2a = win2+len;
    const float *bufa  = buf+len;
    float *sum1a = sum1+len;
    float *sum2a = sum2+len;


#define MULT(a, b)                                 \
    "movaps " #a "(%1,%0), %%xmm1           \n\t"  \
    "movaps " #a "(%3,%0), %%xmm2           \n\t"  \
    "mulps         %%xmm2, %%xmm1           \n\t"  \
    "subps         %%xmm1, %%xmm0           \n\t"  \
    "mulps  " #b "(%2,%0), %%xmm2           \n\t"  \
    "subps         %%xmm2, %%xmm4           \n\t"  \

    __asm__ volatile(
            "1:                                   \n\t"
            "xorps       %%xmm0, %%xmm0           \n\t"
            "xorps       %%xmm4, %%xmm4           \n\t"

            MULT(   0,   0)
            MULT( 256,  64)
            MULT( 512, 128)
            MULT( 768, 192)
            MULT(1024, 256)
            MULT(1280, 320)
            MULT(1536, 384)
            MULT(1792, 448)

            "movaps      %%xmm0, (%4,%0)          \n\t"
            "movaps      %%xmm4, (%5,%0)          \n\t"
            "add            $16,  %0              \n\t"
            "jl              1b                   \n\t"
            :"+&r"(count)
            :"r"(win1a), "r"(win2a), "r"(bufa), "r"(sum1a), "r"(sum2a)
            );

#undef MULT
}

static void apply_window_mp3(float *in, float *win, int *unused, float *out,
                             int incr)
{
    LOCAL_ALIGNED_16(float, suma, [17]);
    LOCAL_ALIGNED_16(float, sumb, [17]);
    LOCAL_ALIGNED_16(float, sumc, [17]);
    LOCAL_ALIGNED_16(float, sumd, [17]);

    float sum;

    /* copy to avoid wrap */
    memcpy(in + 512, in, 32 * sizeof(*in));

    apply_window(in + 16, win     , win + 512, suma, sumc, 16);
    apply_window(in + 32, win + 48, win + 640, sumb, sumd, 16);

    SUM8(MACS, suma[0], win + 32, in + 48);

    sumc[ 0] = 0;
    sumb[16] = 0;
    sumd[16] = 0;

#define SUMS(suma, sumb, sumc, sumd, out1, out2)               \
            "movups " #sumd "(%4),       %%xmm0          \n\t" \
            "shufps         $0x1b,       %%xmm0, %%xmm0  \n\t" \
            "subps  " #suma "(%1),       %%xmm0          \n\t" \
            "movaps        %%xmm0," #out1 "(%0)          \n\t" \
\
            "movups " #sumc "(%3),       %%xmm0          \n\t" \
            "shufps         $0x1b,       %%xmm0, %%xmm0  \n\t" \
            "addps  " #sumb "(%2),       %%xmm0          \n\t" \
            "movaps        %%xmm0," #out2 "(%0)          \n\t"

    if (incr == 1) {
        __asm__ volatile(
            SUMS( 0, 48,  4, 52,  0, 112)
            SUMS(16, 32, 20, 36, 16,  96)
            SUMS(32, 16, 36, 20, 32,  80)
            SUMS(48,  0, 52,  4, 48,  64)

            :"+&r"(out)
            :"r"(&suma[0]), "r"(&sumb[0]), "r"(&sumc[0]), "r"(&sumd[0])
            :"memory"
            );
        out += 16*incr;
    } else {
        int j;
        float *out2 = out + 32 * incr;
        out[0  ]  = -suma[   0];
        out += incr;
        out2 -= incr;
        for(j=1;j<16;j++) {
            *out  = -suma[   j] + sumd[16-j];
            *out2 =  sumb[16-j] + sumc[   j];
            out  += incr;
            out2 -= incr;
        }
    }

    sum = 0;
    SUM8(MLSS, sum, win + 16 + 32, in + 32);
    *out = sum;
}

void ff_mpegaudiodec_init_mmx(MPADecodeContext *s)
{
    int mm_flags = av_get_cpu_flags();

    if (mm_flags & AV_CPU_FLAG_SSE2) {
        s->apply_window_mp3 = apply_window_mp3;
    }
}
