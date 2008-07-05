/*
 * RealAudio 2.0 (28.8K)
 * Copyright (c) 2003 the ffmpeg project
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

#include "avcodec.h"
#define ALT_BITSTREAM_READER_LE
#include "bitstream.h"
#include "ra288.h"

typedef struct {
    float history[8];
    float output[40];
    float pr1[36];
    float pr2[10];
    int   phase, phasep;

    float st1a[111], st1b[37], st1[37];
    float st2a[38], st2b[11], st2[11];
    float sb[41];
    float lhist[10];
} Real288_internal;

static inline float scalar_product_float(float * v1, float * v2, int size)
{
    float res = 0.;

    while (size--)
        res += *v1++ * *v2++;

    return res;
}

/* Decode and produce output */
static void decode(Real288_internal *glob, float gain, int cb_coef)
{
    int x, y;
    double sum, sumsum;
    float buffer[5];

    for (x=35; x >= 0; x--)
        glob->sb[x+5] = glob->sb[x];

    for (x=4; x >= 0; x--) {
        glob->sb[x] = -scalar_product_float(glob->sb + x + 1, glob->pr1, 36);
    }

    /* convert log and do rms */
    sum = 32. - scalar_product_float(glob->pr2, glob->lhist, 10);

    if (sum < 0)
        sum = 0;
    else if (sum > 60)
        sum = 60;

    sumsum = exp(sum * 0.1151292546497) * gain;    /* pow(10.0,sum/20)*f */

    sum = 0;
    for (x=0; x < 5; x++) {
        buffer[x] = codetable[cb_coef][x] * sumsum;
        sum += buffer[x] * buffer[x];
    }

    sum /= 5;
    if (sum < 1)
        sum = 1;

    /* shift and store */
    for (x=10; x > 0; x--)
        glob->lhist[x] = glob->lhist[x-1];

    *glob->lhist = glob->history[glob->phase] = 10 * log10(sum) - 32;

    for (x=1; x < 5; x++)
        for (y=x-1; y >= 0; y--)
            buffer[x] -= glob->pr1[x-y-1] * buffer[y];

    /* output */
    for (x=0; x < 5; x++) {
        float f = glob->sb[4-x] + buffer[x];

        if (f > 4095)
            f = 4095;
        else if (f < -4095)
            f = -4095;

        glob->output[glob->phasep+x] = glob->sb[4-x] = f;
    }
}

/* column multiply */
static void colmult(float *tgt, float *m1, const float *m2, int n)
{
    while (n--)
        *(tgt++) = (*(m1++)) * (*(m2++));
}

static int pred(float *in, float *tgt, int n)
{
    int x, y;
    double f0, f1, f2;

    if (in[n] == 0)
        return 0;

    if ((f0 = *in) <= 0)
        return 0;

    for (x=1 ; ; x++) {
        float *p1 = in + x;
        float *p2 = tgt;

        if (n < x)
            return 1;

        f1 = *(p1--);

        for (y=0; y < x - 1; y++)
            f1 += (*(p1--))*(*(p2++));

        p1 = tgt + x - 1;
        p2 = tgt;
        *(p1--) = f2 = -f1/f0;
        for (y=x >> 1; y--;) {
            float temp = *p2 + *p1 * f2;
            *(p1--) += *p2 * f2;
            *(p2++) = temp;
        }
        if ((f0 += f1*f2) < 0)
            return 0;
    }
}

/* product sum (lsf) */
static void prodsum(float *tgt, float *src, int len, int n)
{
    for (; n >= 0; n--)
        tgt[n] = scalar_product_float(src, src - n, len);

}

static void co(int n, int i, int j, float *in, float *out, float *st1,
               float *st2, const float *table)
{
    int a, b, c;
    unsigned int x;
    float *fp;
    float buffer1[37];
    float buffer2[37];
    float work[111];

    /* rotate and multiply */
    c = (b = (a = n + i) + j) - i;
    fp = st1 + i;
    for (x=0; x < b; x++) {
        if (x == c)
            fp=in;
        work[x] = *(table++) * (*(st1++) = *(fp++));
    }

    prodsum(buffer1, work + n, i, n);
    prodsum(buffer2, work + a, j, n);

    for (x=0;x<=n;x++) {
        *st2 = *st2 * (0.5625) + buffer1[x];
        out[x] = *(st2++) + buffer2[x];
    }
    *out *= 1.00390625; /* to prevent clipping */
}

static void update(Real288_internal *glob)
{
    int x,y;
    float buffer1[40], temp1[37];
    float buffer2[8], temp2[11];

    y = glob->phasep+5;
    for (x=0;  x < 40; x++)
        buffer1[x] = glob->output[(y++)%40];

    co(36, 40, 35, buffer1, temp1, glob->st1a, glob->st1b, table1);

    if (pred(temp1, glob->st1, 36))
        colmult(glob->pr1, glob->st1, table1a, 36);

    y = glob->phase + 1;
    for (x=0; x < 8; x++)
        buffer2[x] = glob->history[(y++) % 8];

    co(10, 8, 20, buffer2, temp2, glob->st2a, glob->st2b, table2);

    if (pred(temp2, glob->st2, 10))
        colmult(glob->pr2, glob->st2, table2a, 10);
}

/* Decode a block (celp) */
static int ra288_decode_frame(AVCodecContext * avctx, void *data,
                              int *data_size, const uint8_t * buf,
                              int buf_size)
{
    int16_t *out = data;
    int x, y;
    Real288_internal *glob = avctx->priv_data;
    GetBitContext gb;

    if (buf_size < avctx->block_align) {
        av_log(avctx, AV_LOG_ERROR,
               "Error! Input buffer is too small [%d<%d]\n",
               buf_size, avctx->block_align);
        return 0;
    }

    init_get_bits(&gb, buf, avctx->block_align * 8);

    for (x=0; x < 32; x++) {
        float gain = amptable[get_bits(&gb, 3)];
        int cb_coef = get_bits(&gb, 6 + (x&1));
        glob->phasep = (glob->phase = x & 7) * 5;
        decode(glob, gain, cb_coef);

        for (y=0; y < 5; y++)
            *(out++) = 8 * glob->output[glob->phasep + y];

        if (glob->phase == 3)
            update(glob);
    }

    *data_size = (char *)out - (char *)data;
    return avctx->block_align;
}

AVCodec ra_288_decoder =
{
    "real_288",
    CODEC_TYPE_AUDIO,
    CODEC_ID_RA_288,
    sizeof(Real288_internal),
    NULL,
    NULL,
    NULL,
    ra288_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("RealAudio 2.0 (28.8K)"),
};
