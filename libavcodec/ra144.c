/*
 * Real Audio 1.0 (14.4K)
 *
 * Copyright (c) 2008 Vitor Sessak
 * Copyright (c) 2003 Nick Kurshev
 *     Based on public domain decoder at http://www.honeypot.net/audio
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
#include "celp_filters.h"

#define NBLOCKS         4       ///< number of subblocks within a block
#define BLOCKSIZE       40      ///< subblock size in 16-bit words
#define BUFFERSIZE      146     ///< the size of the adaptive codebook


typedef struct {
    unsigned int     old_energy;        ///< previous frame energy

    unsigned int     lpc_tables[2][10];

    /** LPC coefficients: lpc_coef[0] is the coefficients of the current frame
     *  and lpc_coef[1] of the previous one. */
    unsigned int    *lpc_coef[2];

    unsigned int     lpc_refl_rms[2];

    /** The current subblock padded by the last 10 values of the previous one. */
    int16_t curr_sblock[50];

    /** Adaptive codebook, its size is two units bigger to avoid a
     *  buffer overflow. */
    uint16_t adapt_cb[146+2];
} RA144Context;

static av_cold int ra144_decode_init(AVCodecContext * avctx)
{
    RA144Context *ractx = avctx->priv_data;

    ractx->lpc_coef[0] = ractx->lpc_tables[0];
    ractx->lpc_coef[1] = ractx->lpc_tables[1];

    avctx->sample_fmt = SAMPLE_FMT_S16;
    return 0;
}

/**
 * Evaluate sqrt(x << 24). x must fit in 20 bits. This value is evaluated in an
 * odd way to make the output identical to the binary decoder.
 */
static int t_sqrt(unsigned int x)
{
    int s = 2;
    while (x > 0xfff) {
        s++;
        x >>= 2;
    }

    return ff_sqrt(x << 20) << s;
}

/**
 * Evaluate the LPC filter coefficients from the reflection coefficients.
 * Does the inverse of the eval_refl() function.
 */
static void eval_coefs(int *coefs, const int *refl)
{
    int buffer[10];
    int *b1 = buffer;
    int *b2 = coefs;
    int i, j;

    for (i=0; i < 10; i++) {
        b1[i] = refl[i] << 4;

        for (j=0; j < i; j++)
            b1[j] = ((refl[i] * b2[i-j-1]) >> 12) + b2[j];

        FFSWAP(int *, b1, b2);
    }

    for (i=0; i < 10; i++)
        coefs[i] >>= 4;
}

/**
 * Copy the last offset values of *source to *target. If those values are not
 * enough to fill the target buffer, fill it with another copy of those values.
 */
static void copy_and_dup(int16_t *target, const int16_t *source, int offset)
{
    source += BUFFERSIZE - offset;

    memcpy(target, source, FFMIN(BLOCKSIZE, offset)*sizeof(*target));
    if (offset < BLOCKSIZE)
        memcpy(target + offset, source, (BLOCKSIZE - offset)*sizeof(*target));
}

/** inverse root mean square */
static int irms(const int16_t *data)
{
    unsigned int i, sum = 0;

    for (i=0; i < BLOCKSIZE; i++)
        sum += data[i] * data[i];

    if (sum == 0)
        return 0; /* OOPS - division by zero */

    return 0x20000000 / (t_sqrt(sum) >> 8);
}

static void add_wav(int16_t *dest, int n, int skip_first, int *m,
                    const int16_t *s1, const int8_t *s2, const int8_t *s3)
{
    int i;
    int v[3];

    v[0] = 0;
    for (i=!skip_first; i<3; i++)
        v[i] = (gain_val_tab[n][i] * m[i]) >> gain_exp_tab[n];

    if (v[0]) {
        for (i=0; i < BLOCKSIZE; i++)
            dest[i] = (s1[i]*v[0] + s2[i]*v[1] + s3[i]*v[2]) >> 12;
    } else {
        for (i=0; i < BLOCKSIZE; i++)
            dest[i] = (             s2[i]*v[1] + s3[i]*v[2]) >> 12;
    }
}

static unsigned int rescale_rms(unsigned int rms, unsigned int energy)
{
    return (rms * energy) >> 10;
}

static unsigned int rms(const int *data)
{
    int i;
    unsigned int res = 0x10000;
    int b = 10;

    for (i=0; i < 10; i++) {
        res = (((0x1000000 - data[i]*data[i]) >> 12) * res) >> 12;

        if (res == 0)
            return 0;

        while (res <= 0x3fff) {
            b++;
            res <<= 2;
        }
    }

    return t_sqrt(res) >> b;
}

static void do_output_subblock(RA144Context *ractx, const uint16_t  *lpc_coefs,
                               int gval, GetBitContext *gb)
{
    uint16_t buffer_a[40];
    uint16_t *block;
    int cba_idx = get_bits(gb, 7); // index of the adaptive CB, 0 if none
    int gain    = get_bits(gb, 8);
    int cb1_idx = get_bits(gb, 7);
    int cb2_idx = get_bits(gb, 7);
    int m[3];

    if (cba_idx) {
        cba_idx += BLOCKSIZE/2 - 1;
        copy_and_dup(buffer_a, ractx->adapt_cb, cba_idx);
        m[0] = (irms(buffer_a) * gval) >> 12;
    } else {
        m[0] = 0;
    }

    m[1] = (cb1_base[cb1_idx] * gval) >> 8;
    m[2] = (cb2_base[cb2_idx] * gval) >> 8;

    memmove(ractx->adapt_cb, ractx->adapt_cb + BLOCKSIZE,
            (BUFFERSIZE - BLOCKSIZE) * sizeof(*ractx->adapt_cb));

    block = ractx->adapt_cb + BUFFERSIZE - BLOCKSIZE;

    add_wav(block, gain, cba_idx, m, cba_idx? buffer_a: NULL,
            cb1_vects[cb1_idx], cb2_vects[cb2_idx]);

    memcpy(ractx->curr_sblock, ractx->curr_sblock + 40,
           10*sizeof(*ractx->curr_sblock));

    if (ff_celp_lp_synthesis_filter(ractx->curr_sblock + 10, lpc_coefs,
                                    block, BLOCKSIZE, 10, 1, 0xfff))
        memset(ractx->curr_sblock, 0, 50*sizeof(*ractx->curr_sblock));
}

static void int_to_int16(int16_t *out, const int *inp)
{
    int i;

    for (i=0; i < 30; i++)
        *out++ = *inp++;
}

/**
 * Evaluate the reflection coefficients from the filter coefficients.
 * Does the inverse of the eval_coefs() function.
 *
 * @return 1 if one of the reflection coefficients is greater than
 *         4095, 0 if not.
 */
static int eval_refl(int *refl, const int16_t *coefs, RA144Context *ractx)
{
    int b, i, j;
    int buffer1[10];
    int buffer2[10];
    int *bp1 = buffer1;
    int *bp2 = buffer2;

    for (i=0; i < 10; i++)
        buffer2[i] = coefs[i];

    refl[9] = bp2[9];

    if ((unsigned) bp2[9] + 0x1000 > 0x1fff) {
        av_log(ractx, AV_LOG_ERROR, "Overflow. Broken sample?\n");
        return 1;
    }

    for (i=8; i >= 0; i--) {
        b = 0x1000-((bp2[i+1] * bp2[i+1]) >> 12);

        if (!b)
            b = -2;

        for (j=0; j <= i; j++)
            bp1[j] = ((bp2[j] - ((refl[i+1] * bp2[i-j]) >> 12)) * (0x1000000 / b)) >> 12;

        if ((unsigned) bp1[i] + 0x1000 > 0x1fff)
            return 1;

        refl[i] = bp1[i];

        FFSWAP(int *, bp1, bp2);
    }
    return 0;
}

static int interp(RA144Context *ractx, int16_t *out, int a,
                  int copyold, int energy)
{
    int work[10];
    int b = NBLOCKS - a;
    int i;

    // Interpolate block coefficients from the this frame's forth block and
    // last frame's forth block.
    for (i=0; i<30; i++)
        out[i] = (a * ractx->lpc_coef[0][i] + b * ractx->lpc_coef[1][i])>> 2;

    if (eval_refl(work, out, ractx)) {
        // The interpolated coefficients are unstable, copy either new or old
        // coefficients.
        int_to_int16(out, ractx->lpc_coef[copyold]);
        return rescale_rms(ractx->lpc_refl_rms[copyold], energy);
    } else {
        return rescale_rms(rms(work), energy);
    }
}

/** Uncompress one block (20 bytes -> 160*2 bytes). */
static int ra144_decode_frame(AVCodecContext * avctx, void *vdata,
                              int *data_size, const uint8_t *buf, int buf_size)
{
    static const uint8_t sizes[10] = {6, 5, 5, 4, 4, 3, 3, 3, 3, 2};
    unsigned int refl_rms[4];    // RMS of the reflection coefficients
    uint16_t block_coefs[4][30]; // LPC coefficients of each sub-block
    unsigned int lpc_refl[10];   // LPC reflection coefficients of the frame
    int i, j;
    int16_t *data = vdata;
    unsigned int energy;

    RA144Context *ractx = avctx->priv_data;
    GetBitContext gb;

    if (*data_size < 2*160)
        return -1;

    if(buf_size < 20) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        *data_size = 0;
        return buf_size;
    }
    init_get_bits(&gb, buf, 20 * 8);

    for (i=0; i<10; i++)
        lpc_refl[i] = lpc_refl_cb[i][get_bits(&gb, sizes[i])];

    eval_coefs(ractx->lpc_coef[0], lpc_refl);
    ractx->lpc_refl_rms[0] = rms(lpc_refl);

    energy = energy_tab[get_bits(&gb, 5)];

    refl_rms[0] = interp(ractx, block_coefs[0], 1, 1, ractx->old_energy);
    refl_rms[1] = interp(ractx, block_coefs[1], 2, energy <= ractx->old_energy,
                    t_sqrt(energy*ractx->old_energy) >> 12);
    refl_rms[2] = interp(ractx, block_coefs[2], 3, 0, energy);
    refl_rms[3] = rescale_rms(ractx->lpc_refl_rms[0], energy);

    int_to_int16(block_coefs[3], ractx->lpc_coef[0]);

    for (i=0; i < 4; i++) {
        do_output_subblock(ractx, block_coefs[i], refl_rms[i], &gb);

        for (j=0; j < BLOCKSIZE; j++)
            *data++ = av_clip_int16(ractx->curr_sblock[j + 10] << 2);
    }

    ractx->old_energy = energy;
    ractx->lpc_refl_rms[1] = ractx->lpc_refl_rms[0];

    FFSWAP(unsigned int *, ractx->lpc_coef[0], ractx->lpc_coef[1]);

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
