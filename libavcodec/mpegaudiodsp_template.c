/*
 * Copyright (c) 2001, 2002 Fabrice Bellard
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

#include <stdint.h>

#include "libavutil/mem.h"
#include "dct32.h"
#include "mathops.h"
#include "mpegaudiodsp.h"
#include "mpegaudio.h"
#include "mpegaudiodata.h"

#if CONFIG_FLOAT
#define RENAME(n) n##_float

static inline float round_sample(float *sum)
{
    float sum1=*sum;
    *sum = 0;
    return sum1;
}

#define MACS(rt, ra, rb) rt+=(ra)*(rb)
#define MULS(ra, rb) ((ra)*(rb))
#define MLSS(rt, ra, rb) rt-=(ra)*(rb)

#else

#define RENAME(n) n##_fixed
#define OUT_SHIFT (WFRAC_BITS + FRAC_BITS - 15)

static inline int round_sample(int64_t *sum)
{
    int sum1;
    sum1 = (int)((*sum) >> OUT_SHIFT);
    *sum &= (1<<OUT_SHIFT)-1;
    return av_clip_int16(sum1);
}

#   define MULS(ra, rb) MUL64(ra, rb)
#   define MACS(rt, ra, rb) MAC64(rt, ra, rb)
#   define MLSS(rt, ra, rb) MLS64(rt, ra, rb)
#endif

DECLARE_ALIGNED(16, MPA_INT, RENAME(ff_mpa_synth_window))[512+256];

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

#define SUM8P2(sum1, op1, sum2, op2, w1, w2, p) \
{                                               \
    INTFLOAT tmp;\
    tmp = p[0 * 64];\
    op1(sum1, (w1)[0 * 64], tmp);\
    op2(sum2, (w2)[0 * 64], tmp);\
    tmp = p[1 * 64];\
    op1(sum1, (w1)[1 * 64], tmp);\
    op2(sum2, (w2)[1 * 64], tmp);\
    tmp = p[2 * 64];\
    op1(sum1, (w1)[2 * 64], tmp);\
    op2(sum2, (w2)[2 * 64], tmp);\
    tmp = p[3 * 64];\
    op1(sum1, (w1)[3 * 64], tmp);\
    op2(sum2, (w2)[3 * 64], tmp);\
    tmp = p[4 * 64];\
    op1(sum1, (w1)[4 * 64], tmp);\
    op2(sum2, (w2)[4 * 64], tmp);\
    tmp = p[5 * 64];\
    op1(sum1, (w1)[5 * 64], tmp);\
    op2(sum2, (w2)[5 * 64], tmp);\
    tmp = p[6 * 64];\
    op1(sum1, (w1)[6 * 64], tmp);\
    op2(sum2, (w2)[6 * 64], tmp);\
    tmp = p[7 * 64];\
    op1(sum1, (w1)[7 * 64], tmp);\
    op2(sum2, (w2)[7 * 64], tmp);\
}

void RENAME(ff_mpadsp_apply_window)(MPA_INT *synth_buf, MPA_INT *window,
                                  int *dither_state, OUT_INT *samples,
                                  int incr)
{
    register const MPA_INT *w, *w2, *p;
    int j;
    OUT_INT *samples2;
#if CONFIG_FLOAT
    float sum, sum2;
#else
    int64_t sum, sum2;
#endif

    /* copy to avoid wrap */
    memcpy(synth_buf + 512, synth_buf, 32 * sizeof(*synth_buf));

    samples2 = samples + 31 * incr;
    w = window;
    w2 = window + 31;

    sum = *dither_state;
    p = synth_buf + 16;
    SUM8(MACS, sum, w, p);
    p = synth_buf + 48;
    SUM8(MLSS, sum, w + 32, p);
    *samples = round_sample(&sum);
    samples += incr;
    w++;

    /* we calculate two samples at the same time to avoid one memory
       access per two sample */
    for(j=1;j<16;j++) {
        sum2 = 0;
        p = synth_buf + 16 + j;
        SUM8P2(sum, MACS, sum2, MLSS, w, w2, p);
        p = synth_buf + 48 - j;
        SUM8P2(sum, MLSS, sum2, MLSS, w + 32, w2 + 32, p);

        *samples = round_sample(&sum);
        samples += incr;
        sum += sum2;
        *samples2 = round_sample(&sum);
        samples2 -= incr;
        w++;
        w2--;
    }

    p = synth_buf + 32;
    SUM8(MLSS, sum, w + 32, p);
    *samples = round_sample(&sum);
    *dither_state= sum;
}

/* 32 sub band synthesis filter. Input: 32 sub band samples, Output:
   32 samples. */
void RENAME(ff_mpa_synth_filter)(MPADSPContext *s, MPA_INT *synth_buf_ptr,
                                 int *synth_buf_offset,
                                 MPA_INT *window, int *dither_state,
                                 OUT_INT *samples, int incr,
                                 MPA_INT *sb_samples)
{
    MPA_INT *synth_buf;
    int offset;

    offset = *synth_buf_offset;
    synth_buf = synth_buf_ptr + offset;

    s->RENAME(dct32)(synth_buf, sb_samples);
    s->RENAME(apply_window)(synth_buf, window, dither_state, samples, incr);

    offset = (offset - 32) & 511;
    *synth_buf_offset = offset;
}

void av_cold RENAME(ff_mpa_synth_init)(MPA_INT *window)
{
    int i, j;

    /* max = 18760, max sum over all 16 coefs : 44736 */
    for(i=0;i<257;i++) {
        INTFLOAT v;
        v = ff_mpa_enwindow[i];
#if CONFIG_FLOAT
        v *= 1.0 / (1LL<<(16 + FRAC_BITS));
#endif
        window[i] = v;
        if ((i & 63) != 0)
            v = -v;
        if (i != 0)
            window[512 - i] = v;
    }

    // Needed for avoiding shuffles in ASM implementations
    for(i=0; i < 8; i++)
        for(j=0; j < 16; j++)
            window[512+16*i+j] = window[64*i+32-j];

    for(i=0; i < 8; i++)
        for(j=0; j < 16; j++)
            window[512+128+16*i+j] = window[64*i+48-j];
}
