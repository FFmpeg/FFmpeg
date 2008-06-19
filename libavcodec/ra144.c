/*
 * Real Audio 1.0 (14.4K)
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
#include "bitstream.h"
#include "ra144.h"

#define NBLOCKS         4       /* number of segments within a block */
#define BLOCKSIZE       40      /* (quarter) block size in 16-bit words (80 bytes) */
#define HALFBLOCK       20      /* BLOCKSIZE/2 */
#define BUFFERSIZE      146     /* for do_output */


typedef struct {
    unsigned int     old_energy;        ///< previous frame energy

    /* the swapped buffers */
    unsigned int     lpc_tables[2][10];
    unsigned int    *lpc_coef;          ///< LPC coefficients
    unsigned int    *lpc_coef_old;      ///< previous frame LPC coefficients
    unsigned int     lpc_refl_rms;
    unsigned int     lpc_refl_rms_old;

    unsigned int buffer[5];
    uint16_t adapt_cb[148];             ///< adaptive codebook
} RA144Context;

static int ra144_decode_init(AVCodecContext * avctx)
{
    RA144Context *ractx = avctx->priv_data;

    ractx->lpc_coef     = ractx->lpc_tables[0];
    ractx->lpc_coef_old = ractx->lpc_tables[1];

    return 0;
}

/**
 * Evaluate sqrt(x << 24). x must fit in 20 bits. This value is evaluated in an
 * odd way to make the output identical to the binary decoder.
 */
static int t_sqrt(unsigned int x)
{
    int s = 0;
    while (x > 0xfff) {
        s++;
        x = x >> 2;
    }

    return (ff_sqrt(x << 20) << s) << 2;
}

/**
 * Evaluate the LPC filter coefficients from the reflection coefficients.
 * Does the inverse of the eval_refl() function.
 */
static void eval_coefs(const int *refl, int *coefs)
{
    int buffer[10];
    int *b1 = buffer;
    int *b2 = coefs;
    int x, y;

    for (x=0; x < 10; x++) {
        b1[x] = refl[x] << 4;

        for (y=0; y < x; y++)
            b1[y] = ((refl[x] * b2[x-y-1]) >> 12) + b2[y];

        FFSWAP(int *, b1, b2);
    }

    for (x=0; x < 10; x++)
        coefs[x] >>= 4;
}

/* rotate block */
static void rotate_block(const int16_t *source, int16_t *target, int offset)
{
    int i=0, k=0;
    source += BUFFERSIZE - offset;

    while (i<BLOCKSIZE) {
        target[i++] = source[k++];

        if (k == offset)
            k = 0;
    }
}

/* inverse root mean square */
static int irms(const int16_t *data, int factor)
{
    unsigned int i, sum = 0;

    for (i=0; i < BLOCKSIZE; i++)
        sum += data[i] * data[i];

    if (sum == 0)
        return 0; /* OOPS - division by zero */

    return (0x20000000 / (t_sqrt(sum) >> 8)) * factor;
}

/* multiply/add wavetable */
static void add_wav(int n, int skip_first, int *m, const int16_t *s1,
                    const int8_t *s2, const int8_t *s3, int16_t *dest)
{
    int i;
    int v[3];

    v[0] = 0;
    for (i=!skip_first; i<3; i++)
        v[i] = (gain_val_tab[n][i] * m[i]) >> (gain_exp_tab[n][i] + 1);

    for (i=0; i < BLOCKSIZE; i++)
        dest[i] = ((*(s1++))*v[0] + (*(s2++))*v[1] + (*(s3++))*v[2]) >> 12;
}

static void lpc_filter(const int16_t *lpc_coefs, const int16_t *adapt_coef,
                       void *out, int *statbuf, int len)
{
    int x, i;
    uint16_t work[50];
    int16_t *ptr = work;

    memcpy(work, statbuf,20);
    memcpy(work + 10, adapt_coef, len * 2);

    for (i=0; i<len; i++) {
        int sum = 0;
        int new_val;

        for(x=0; x<10; x++)
            sum += lpc_coefs[9-x] * ptr[x];

        sum >>= 12;

        new_val = ptr[10] - sum;

        if (new_val < -32768 || new_val > 32767) {
            memset(out, 0, len * 2);
            memset(statbuf, 0, 20);
            return;
        }

        ptr[10] = new_val;
        ptr++;
    }

    memcpy(out, work+10, len * 2);
    memcpy(statbuf, work + 40, 20);
}

static unsigned int rescale_rms(int rms, int energy)
{
    return (rms * energy) >> 10;
}

static unsigned int rms(const int *data)
{
    int x;
    unsigned int res = 0x10000;
    int b = 0;

    for (x=0; x<10; x++) {
        res = (((0x1000000 - (*data) * (*data)) >> 12) * res) >> 12;

        if (res == 0)
            return 0;

        while (res <= 0x3fff) {
            b++;
            res <<= 2;
        }
        data++;
    }

    if (res > 0)
        res = t_sqrt(res);

    res >>= (b + 10);
    return res;
}

/* do quarter-block output */
static void do_output_subblock(RA144Context *ractx,
                               const uint16_t  *lpc_coefs, unsigned int gval,
                               int16_t *output_buffer, GetBitContext *gb)
{
    uint16_t buffer_a[40];
    uint16_t *block;
    int cba_idx = get_bits(gb, 7); // index of the adaptive CB, 0 if none
    int gain    = get_bits(gb, 8);
    int cb1_idx = get_bits(gb, 7);
    int cb2_idx = get_bits(gb, 7);
    int m[3];

    if (cba_idx) {
        cba_idx += HALFBLOCK - 1;
        rotate_block(ractx->adapt_cb, buffer_a, cba_idx);
        m[0] = irms(buffer_a, gval) >> 12;
    } else {
        m[0] = 0;
    }

    m[1] = ((cb1_base[cb1_idx] >> 4) * gval) >> 8;
    m[2] = ((cb2_base[cb2_idx] >> 4) * gval) >> 8;

    memmove(ractx->adapt_cb, ractx->adapt_cb + BLOCKSIZE,
            (BUFFERSIZE - BLOCKSIZE) * 2);

    block = ractx->adapt_cb + BUFFERSIZE - BLOCKSIZE;

    add_wav(gain, cba_idx, m, buffer_a, cb1_vects[cb1_idx], cb2_vects[cb2_idx],
            block);

    lpc_filter(lpc_coefs, block, output_buffer, ractx->buffer, BLOCKSIZE);
}

static void int_to_int16(int16_t *out, const int *inp)
{
    int i;

    for (i=0; i<30; i++)
        *(out++) = *(inp++);
}

/**
 * Evaluate the reflection coefficients from the filter coefficients.
 * Does the inverse of the eval_coefs() function.
 *
 * @return 1 if one of the reflection coefficients is of magnitude greater than
 *         4095, 0 if not.
 */
static int eval_refl(const int16_t *coefs, int *refl, RA144Context *ractx)
{
    int retval = 0;
    int b, c, i;
    unsigned int u;
    int buffer1[10];
    int buffer2[10];
    int *bp1 = buffer1;
    int *bp2 = buffer2;

    for (i=0; i < 10; i++)
        buffer2[i] = coefs[i];

    u = refl[9] = bp2[9];

    if (u + 0x1000 > 0x1fff) {
        av_log(ractx, AV_LOG_ERROR, "Overflow. Broken sample?\n");
        return 0;
    }

    for (c=8; c >= 0; c--) {
        if (u == 0x1000)
            u++;

        if (u == 0xfffff000)
            u--;

        b = 0x1000-((u * u) >> 12);

        if (b == 0)
            b++;

        for (u=0; u<=c; u++)
            bp1[u] = ((bp2[u] - ((refl[c+1] * bp2[c-u]) >> 12)) * (0x1000000 / b)) >> 12;

        refl[c] = u = bp1[c];

        if ((u + 0x1000) > 0x1fff)
            retval = 1;

        FFSWAP(int *, bp1, bp2);
    }
    return retval;
}

static int interp(RA144Context *ractx, int16_t *out, int block_num,
                  int copynew, int energy)
{
    int work[10];
    int a = block_num + 1;
    int b = NBLOCKS - a;
    int x;

    // Interpolate block coefficients from the this frame forth block and
    // last frame forth block
    for (x=0; x<30; x++)
        out[x] = (a * ractx->lpc_coef[x] + b * ractx->lpc_coef_old[x])>> 2;

    if (eval_refl(out, work, ractx)) {
        // The interpolated coefficients are unstable, copy either new or old
        // coefficients
        if (copynew) {
            int_to_int16(out, ractx->lpc_coef);
            return rescale_rms(ractx->lpc_refl_rms, energy);
        } else {
            int_to_int16(out, ractx->lpc_coef_old);
            return rescale_rms(ractx->lpc_refl_rms_old, energy);
        }
    } else {
        return rescale_rms(rms(work), energy);
    }
}

/* Uncompress one block (20 bytes -> 160*2 bytes) */
static int ra144_decode_frame(AVCodecContext * avctx,
                              void *vdata, int *data_size,
                              const uint8_t * buf, int buf_size)
{
    static const uint8_t sizes[10] = {6, 5, 5, 4, 4, 3, 3, 3, 3, 2};
    unsigned int refl_rms[4];    // RMS of the reflection coefficients
    uint16_t block_coefs[4][30]; // LPC coefficients of each sub-block
    unsigned int lpc_refl[10];   // LPC reflection coefficients of the frame
    int i, c;
    int16_t *data = vdata;
    unsigned int energy;

    RA144Context *ractx = avctx->priv_data;
    GetBitContext gb;

    if(buf_size < 20) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return buf_size;
    }
    init_get_bits(&gb, buf, 20 * 8);

    for (i=0; i<10; i++)
        // "<< 1"? Doesn't this make one value out of two of the table useless?
        lpc_refl[i] = lpc_refl_cb[i][get_bits(&gb, sizes[i]) << 1];

    eval_coefs(lpc_refl, ractx->lpc_coef);
    ractx->lpc_refl_rms = rms(lpc_refl);

    energy = energy_tab[get_bits(&gb, 5) << 1]; // Useless table entries?

    refl_rms[0] = interp(ractx, block_coefs[0], 0, 0, ractx->old_energy);
    refl_rms[1] = interp(ractx, block_coefs[1], 1, energy > ractx->old_energy,
                    t_sqrt(energy*ractx->old_energy) >> 12);
    refl_rms[2] = interp(ractx, block_coefs[2], 2, 1, energy);
    refl_rms[3] = rescale_rms(ractx->lpc_refl_rms, energy);

    int_to_int16(block_coefs[3], ractx->lpc_coef);

    /* do output */
    for (c=0; c<4; c++) {
        do_output_subblock(ractx, block_coefs[c], refl_rms[c], data, &gb);

        for (i=0; i<BLOCKSIZE; i++) {
            *data = av_clip_int16(*data << 2);
            data++;
        }
    }

    ractx->old_energy = energy;
    ractx->lpc_refl_rms_old = ractx->lpc_refl_rms;

    FFSWAP(unsigned int *, ractx->lpc_coef_old, ractx->lpc_coef);

    *data_size = 2*160;
    return 20;
}

AVCodec ra_144_decoder =
{
    "real_144",
    CODEC_TYPE_AUDIO,
    CODEC_ID_RA_144,
    sizeof(RA144Context),
    ra144_decode_init,
    NULL,
    NULL,
    ra144_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("RealAudio 1.0 (14.4K)"),
};
