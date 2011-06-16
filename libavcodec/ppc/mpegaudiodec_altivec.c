/*
 * Altivec optimized MP3 decoding functions
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

#include "dsputil_altivec.h"
#include "util_altivec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegaudiodsp.h"

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
    const vector float *win1a = (const vector float *) win1;
    const vector float *win2a = (const vector float *) win2;
    const vector float *bufa  = (const vector float *) buf;
    vector float *sum1a = (vector float *) sum1;
    vector float *sum2a = (vector float *) sum2;
    vector float av_uninit(v0), av_uninit(v4);
    vector float v1, v2, v3;

    len = len >> 2;

#define MULT(a, b)                         \
    {                                      \
        v1 = vec_ld(a, win1a);             \
        v2 = vec_ld(b, win2a);             \
        v3 = vec_ld(a, bufa);              \
        v0 = vec_madd(v3, v1, v0);         \
        v4 = vec_madd(v2, v3, v4);         \
    }

    while (len--) {
        v0 = vec_xor(v0, v0);
        v4 = vec_xor(v4, v4);

        MULT(   0,   0);
        MULT( 256,  64);
        MULT( 512, 128);
        MULT( 768, 192);
        MULT(1024, 256);
        MULT(1280, 320);
        MULT(1536, 384);
        MULT(1792, 448);

        vec_st(v0, 0, sum1a);
        vec_st(v4, 0, sum2a);
        sum1a++;
        sum2a++;
        win1a++;
        win2a++;
        bufa++;
    }
}

static void apply_window_mp3(float *in, float *win, int *unused, float *out,
                             int incr)
{
    LOCAL_ALIGNED_16(float, suma, [17]);
    LOCAL_ALIGNED_16(float, sumb, [17]);
    LOCAL_ALIGNED_16(float, sumc, [17]);
    LOCAL_ALIGNED_16(float, sumd, [17]);

    float sum;
    int j;
    float *out2 = out + 32 * incr;

    /* copy to avoid wrap */
    memcpy(in + 512, in, 32 * sizeof(*in));

    apply_window(in + 16, win     , win + 512, suma, sumc, 16);
    apply_window(in + 32, win + 48, win + 640, sumb, sumd, 16);

    SUM8(MLSS, suma[0], win + 32, in + 48);

    sumc[ 0] = 0;
    sumb[16] = 0;
    sumd[16] = 0;

    out[0  ]  = suma[   0];
    out += incr;
    out2 -= incr;
    for(j=1;j<16;j++) {
        *out  =  suma[   j] - sumd[16-j];
        *out2 = -sumb[16-j] - sumc[   j];
        out  += incr;
        out2 -= incr;
    }

    sum = 0;
    SUM8(MLSS, sum, win + 16 + 32, in + 32);
    *out = sum;
}

void ff_mpadsp_init_altivec(MPADSPContext *s)
{
    s->apply_window_float = apply_window_mp3;
}
