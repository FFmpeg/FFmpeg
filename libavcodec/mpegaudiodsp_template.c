/*
 * Copyright (c) 2001, 2002 Fabrice Bellard
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "dct32.h"
#include "mathops.h"
#include "mpegaudiodsp.h"
#include "mpegaudio.h"

#if USE_FLOATS
#define RENAME(n) n##_float

static inline float round_sample(float *sum)
{
    float sum1=*sum;
    *sum = 0;
    return sum1;
}

#define MACS(rt, ra, rb) rt+=(ra)*(rb)
#define MULS(ra, rb) ((ra)*(rb))
#define MULH3(x, y, s) ((s)*(y)*(x))
#define MLSS(rt, ra, rb) rt-=(ra)*(rb)
#define MULLx(x, y, s) ((y)*(x))
#define FIXHR(x)        ((float)(x))
#define FIXR(x)        ((float)(x))
#define SHR(a,b)       ((a)*(1.0f/(1<<(b))))

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
#   define MULH3(x, y, s) MULH((s)*(x), y)
#   define MULLx(x, y, s) MULL((int)(x),(y),s)
#   define SHR(a,b)       (((int)(a))>>(b))
#   define FIXR(a)        ((int)((a) * FRAC_ONE + 0.5))
#   define FIXHR(a)       ((int)((a) * (1LL<<32) + 0.5))
#endif

/** Window for MDCT. Actually only the elements in [0,17] and
    [MDCT_BUF_SIZE/2, MDCT_BUF_SIZE/2 + 17] are actually used. The rest
    is just to preserve alignment for SIMD implementations.
*/
DECLARE_ALIGNED(16, INTFLOAT, RENAME(ff_mdct_win))[8][MDCT_BUF_SIZE];

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
                                  ptrdiff_t incr)
{
    register const MPA_INT *w, *w2, *p;
    int j;
    OUT_INT *samples2;
#if USE_FLOATS
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
                                 OUT_INT *samples, ptrdiff_t incr,
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

static av_cold void mpa_synth_init(MPA_INT *window)
{
    int i, j;

    /* max = 18760, max sum over all 16 coefs : 44736 */
    for(i=0;i<257;i++) {
        INTFLOAT v;
        v = ff_mpa_enwindow[i];
#if USE_FLOATS
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

static av_cold void mpa_synth_window_init(void)
{
    mpa_synth_init(RENAME(ff_mpa_synth_window));
}

av_cold void RENAME(ff_mpa_synth_init)(void)
{
    static AVOnce init_static_once = AV_ONCE_INIT;
    ff_thread_once(&init_static_once, mpa_synth_window_init);
}

/* cos(pi*i/18) */
#define C1 FIXHR(0.98480775301220805936/2)
#define C2 FIXHR(0.93969262078590838405/2)
#define C3 FIXHR(0.86602540378443864676/2)
#define C4 FIXHR(0.76604444311897803520/2)
#define C5 FIXHR(0.64278760968653932632/2)
#define C6 FIXHR(0.5/2)
#define C7 FIXHR(0.34202014332566873304/2)
#define C8 FIXHR(0.17364817766693034885/2)

/* 0.5 / cos(pi*(2*i+1)/36) */
static const INTFLOAT icos36[9] = {
    FIXR(0.50190991877167369479),
    FIXR(0.51763809020504152469), //0
    FIXR(0.55168895948124587824),
    FIXR(0.61038729438072803416),
    FIXR(0.70710678118654752439), //1
    FIXR(0.87172339781054900991),
    FIXR(1.18310079157624925896),
    FIXR(1.93185165257813657349), //2
    FIXR(5.73685662283492756461),
};

/* 0.5 / cos(pi*(2*i+1)/36) */
static const INTFLOAT icos36h[9] = {
    FIXHR(0.50190991877167369479/2),
    FIXHR(0.51763809020504152469/2), //0
    FIXHR(0.55168895948124587824/2),
    FIXHR(0.61038729438072803416/2),
    FIXHR(0.70710678118654752439/2), //1
    FIXHR(0.87172339781054900991/2),
    FIXHR(1.18310079157624925896/4),
    FIXHR(1.93185165257813657349/4), //2
//    FIXHR(5.73685662283492756461),
};

/* using Lee like decomposition followed by hand coded 9 points DCT */
static void imdct36(INTFLOAT *out, INTFLOAT *buf, SUINTFLOAT *in, INTFLOAT *win)
{
    int i, j;
    SUINTFLOAT t0, t1, t2, t3, s0, s1, s2, s3;
    SUINTFLOAT tmp[18], *tmp1, *in1;

    for (i = 17; i >= 1; i--)
        in[i] += in[i-1];
    for (i = 17; i >= 3; i -= 2)
        in[i] += in[i-2];

    for (j = 0; j < 2; j++) {
        tmp1 = tmp + j;
        in1 = in + j;

        t2 = in1[2*4] + in1[2*8] - in1[2*2];

        t3 = in1[2*0] + SHR(in1[2*6],1);
        t1 = in1[2*0] - in1[2*6];
        tmp1[ 6] = t1 - SHR(t2,1);
        tmp1[16] = t1 + t2;

        t0 = MULH3(in1[2*2] + in1[2*4] ,    C2, 2);
        t1 = MULH3(in1[2*4] - in1[2*8] , -2*C8, 1);
        t2 = MULH3(in1[2*2] + in1[2*8] ,   -C4, 2);

        tmp1[10] = t3 - t0 - t2;
        tmp1[ 2] = t3 + t0 + t1;
        tmp1[14] = t3 + t2 - t1;

        tmp1[ 4] = MULH3(in1[2*5] + in1[2*7] - in1[2*1], -C3, 2);
        t2 = MULH3(in1[2*1] + in1[2*5],    C1, 2);
        t3 = MULH3(in1[2*5] - in1[2*7], -2*C7, 1);
        t0 = MULH3(in1[2*3], C3, 2);

        t1 = MULH3(in1[2*1] + in1[2*7],   -C5, 2);

        tmp1[ 0] = t2 + t3 + t0;
        tmp1[12] = t2 + t1 - t0;
        tmp1[ 8] = t3 - t1 - t0;
    }

    i = 0;
    for (j = 0; j < 4; j++) {
        t0 = tmp[i];
        t1 = tmp[i + 2];
        s0 = t1 + t0;
        s2 = t1 - t0;

        t2 = tmp[i + 1];
        t3 = tmp[i + 3];
        s1 = MULH3(t3 + t2, icos36h[    j], 2);
        s3 = MULLx(t3 - t2, icos36 [8 - j], FRAC_BITS);

        t0 = s0 + s1;
        t1 = s0 - s1;
        out[(9 + j) * SBLIMIT] = MULH3(t1, win[     9 + j], 1) + buf[4*(9 + j)];
        out[(8 - j) * SBLIMIT] = MULH3(t1, win[     8 - j], 1) + buf[4*(8 - j)];
        buf[4 * ( 9 + j     )] = MULH3(t0, win[MDCT_BUF_SIZE/2 + 9 + j], 1);
        buf[4 * ( 8 - j     )] = MULH3(t0, win[MDCT_BUF_SIZE/2 + 8 - j], 1);

        t0 = s2 + s3;
        t1 = s2 - s3;
        out[(9 + 8 - j) * SBLIMIT] = MULH3(t1, win[     9 + 8 - j], 1) + buf[4*(9 + 8 - j)];
        out[         j  * SBLIMIT] = MULH3(t1, win[             j], 1) + buf[4*(        j)];
        buf[4 * ( 9 + 8 - j     )] = MULH3(t0, win[MDCT_BUF_SIZE/2 + 9 + 8 - j], 1);
        buf[4 * (         j     )] = MULH3(t0, win[MDCT_BUF_SIZE/2         + j], 1);
        i += 4;
    }

    s0 = tmp[16];
    s1 = MULH3(tmp[17], icos36h[4], 2);
    t0 = s0 + s1;
    t1 = s0 - s1;
    out[(9 + 4) * SBLIMIT] = MULH3(t1, win[     9 + 4], 1) + buf[4*(9 + 4)];
    out[(8 - 4) * SBLIMIT] = MULH3(t1, win[     8 - 4], 1) + buf[4*(8 - 4)];
    buf[4 * ( 9 + 4     )] = MULH3(t0, win[MDCT_BUF_SIZE/2 + 9 + 4], 1);
    buf[4 * ( 8 - 4     )] = MULH3(t0, win[MDCT_BUF_SIZE/2 + 8 - 4], 1);
}

void RENAME(ff_imdct36_blocks)(INTFLOAT *out, INTFLOAT *buf, INTFLOAT *in,
                               int count, int switch_point, int block_type)
{
    int j;
    for (j=0 ; j < count; j++) {
        /* apply window & overlap with previous buffer */

        /* select window */
        int win_idx = (switch_point && j < 2) ? 0 : block_type;
        INTFLOAT *win = RENAME(ff_mdct_win)[win_idx + (4 & -(j & 1))];

        imdct36(out, buf, in, win);

        in  += 18;
        buf += ((j&3) != 3 ? 1 : (72-3));
        out++;
    }
}

