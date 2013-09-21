/*
 * G.723.1 compatible decoder
 * Copyright (c) 2006 Benjamin Larsson
 * Copyright (c) 2010 Mohamed Naufal Basheer
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

/**
 * @file
 * G.723.1 compatible decoder
 */

#define BITSTREAM_READER_LE
#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "get_bits.h"
#include "acelp_vectors.h"
#include "celp_filters.h"
#include "celp_math.h"
#include "g723_1_data.h"
#include "internal.h"

#define CNG_RANDOM_SEED 12345

typedef struct g723_1_context {
    AVClass *class;

    G723_1_Subframe subframe[4];
    enum FrameType cur_frame_type;
    enum FrameType past_frame_type;
    enum Rate cur_rate;
    uint8_t lsp_index[LSP_BANDS];
    int pitch_lag[2];
    int erased_frames;

    int16_t prev_lsp[LPC_ORDER];
    int16_t sid_lsp[LPC_ORDER];
    int16_t prev_excitation[PITCH_MAX];
    int16_t excitation[PITCH_MAX + FRAME_LEN + 4];
    int16_t synth_mem[LPC_ORDER];
    int16_t fir_mem[LPC_ORDER];
    int     iir_mem[LPC_ORDER];

    int random_seed;
    int cng_random_seed;
    int interp_index;
    int interp_gain;
    int sid_gain;
    int cur_gain;
    int reflection_coef;
    int pf_gain;                 ///< formant postfilter
                                 ///< gain scaling unit memory
    int postfilter;

    int16_t audio[FRAME_LEN + LPC_ORDER + PITCH_MAX + 4];
    int16_t prev_data[HALF_FRAME_LEN];
    int16_t prev_weight_sig[PITCH_MAX];


    int16_t hpf_fir_mem;                   ///< highpass filter fir
    int     hpf_iir_mem;                   ///< and iir memories
    int16_t perf_fir_mem[LPC_ORDER];       ///< perceptual filter fir
    int16_t perf_iir_mem[LPC_ORDER];       ///< and iir memories

    int16_t harmonic_mem[PITCH_MAX];
} G723_1_Context;

static av_cold int g723_1_decode_init(AVCodecContext *avctx)
{
    G723_1_Context *p = avctx->priv_data;

    avctx->channel_layout = AV_CH_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;
    avctx->channels       = 1;
    p->pf_gain            = 1 << 12;

    memcpy(p->prev_lsp, dc_lsp, LPC_ORDER * sizeof(*p->prev_lsp));
    memcpy(p->sid_lsp,  dc_lsp, LPC_ORDER * sizeof(*p->sid_lsp));

    p->cng_random_seed = CNG_RANDOM_SEED;
    p->past_frame_type = SID_FRAME;

    return 0;
}

/**
 * Unpack the frame into parameters.
 *
 * @param p           the context
 * @param buf         pointer to the input buffer
 * @param buf_size    size of the input buffer
 */
static int unpack_bitstream(G723_1_Context *p, const uint8_t *buf,
                            int buf_size)
{
    GetBitContext gb;
    int ad_cb_len;
    int temp, info_bits, i;

    init_get_bits(&gb, buf, buf_size * 8);

    /* Extract frame type and rate info */
    info_bits = get_bits(&gb, 2);

    if (info_bits == 3) {
        p->cur_frame_type = UNTRANSMITTED_FRAME;
        return 0;
    }

    /* Extract 24 bit lsp indices, 8 bit for each band */
    p->lsp_index[2] = get_bits(&gb, 8);
    p->lsp_index[1] = get_bits(&gb, 8);
    p->lsp_index[0] = get_bits(&gb, 8);

    if (info_bits == 2) {
        p->cur_frame_type = SID_FRAME;
        p->subframe[0].amp_index = get_bits(&gb, 6);
        return 0;
    }

    /* Extract the info common to both rates */
    p->cur_rate       = info_bits ? RATE_5300 : RATE_6300;
    p->cur_frame_type = ACTIVE_FRAME;

    p->pitch_lag[0] = get_bits(&gb, 7);
    if (p->pitch_lag[0] > 123)       /* test if forbidden code */
        return -1;
    p->pitch_lag[0] += PITCH_MIN;
    p->subframe[1].ad_cb_lag = get_bits(&gb, 2);

    p->pitch_lag[1] = get_bits(&gb, 7);
    if (p->pitch_lag[1] > 123)
        return -1;
    p->pitch_lag[1] += PITCH_MIN;
    p->subframe[3].ad_cb_lag = get_bits(&gb, 2);
    p->subframe[0].ad_cb_lag = 1;
    p->subframe[2].ad_cb_lag = 1;

    for (i = 0; i < SUBFRAMES; i++) {
        /* Extract combined gain */
        temp = get_bits(&gb, 12);
        ad_cb_len = 170;
        p->subframe[i].dirac_train = 0;
        if (p->cur_rate == RATE_6300 && p->pitch_lag[i >> 1] < SUBFRAME_LEN - 2) {
            p->subframe[i].dirac_train = temp >> 11;
            temp &= 0x7FF;
            ad_cb_len = 85;
        }
        p->subframe[i].ad_cb_gain = FASTDIV(temp, GAIN_LEVELS);
        if (p->subframe[i].ad_cb_gain < ad_cb_len) {
            p->subframe[i].amp_index = temp - p->subframe[i].ad_cb_gain *
                                       GAIN_LEVELS;
        } else {
            return -1;
        }
    }

    p->subframe[0].grid_index = get_bits1(&gb);
    p->subframe[1].grid_index = get_bits1(&gb);
    p->subframe[2].grid_index = get_bits1(&gb);
    p->subframe[3].grid_index = get_bits1(&gb);

    if (p->cur_rate == RATE_6300) {
        skip_bits1(&gb);  /* skip reserved bit */

        /* Compute pulse_pos index using the 13-bit combined position index */
        temp = get_bits(&gb, 13);
        p->subframe[0].pulse_pos = temp / 810;

        temp -= p->subframe[0].pulse_pos * 810;
        p->subframe[1].pulse_pos = FASTDIV(temp, 90);

        temp -= p->subframe[1].pulse_pos * 90;
        p->subframe[2].pulse_pos = FASTDIV(temp, 9);
        p->subframe[3].pulse_pos = temp - p->subframe[2].pulse_pos * 9;

        p->subframe[0].pulse_pos = (p->subframe[0].pulse_pos << 16) +
                                   get_bits(&gb, 16);
        p->subframe[1].pulse_pos = (p->subframe[1].pulse_pos << 14) +
                                   get_bits(&gb, 14);
        p->subframe[2].pulse_pos = (p->subframe[2].pulse_pos << 16) +
                                   get_bits(&gb, 16);
        p->subframe[3].pulse_pos = (p->subframe[3].pulse_pos << 14) +
                                   get_bits(&gb, 14);

        p->subframe[0].pulse_sign = get_bits(&gb, 6);
        p->subframe[1].pulse_sign = get_bits(&gb, 5);
        p->subframe[2].pulse_sign = get_bits(&gb, 6);
        p->subframe[3].pulse_sign = get_bits(&gb, 5);
    } else { /* 5300 bps */
        p->subframe[0].pulse_pos  = get_bits(&gb, 12);
        p->subframe[1].pulse_pos  = get_bits(&gb, 12);
        p->subframe[2].pulse_pos  = get_bits(&gb, 12);
        p->subframe[3].pulse_pos  = get_bits(&gb, 12);

        p->subframe[0].pulse_sign = get_bits(&gb, 4);
        p->subframe[1].pulse_sign = get_bits(&gb, 4);
        p->subframe[2].pulse_sign = get_bits(&gb, 4);
        p->subframe[3].pulse_sign = get_bits(&gb, 4);
    }

    return 0;
}

/**
 * Bitexact implementation of sqrt(val/2).
 */
static int16_t square_root(unsigned val)
{
    av_assert2(!(val & 0x80000000));

    return (ff_sqrt(val << 1) >> 1) & (~1);
}

/**
 * Calculate the number of left-shifts required for normalizing the input.
 *
 * @param num   input number
 * @param width width of the input, 15 or 31 bits
 */
static int normalize_bits(int num, int width)
{
    return width - av_log2(num) - 1;
}

#define normalize_bits_int16(num) normalize_bits(num, 15)
#define normalize_bits_int32(num) normalize_bits(num, 31)

/**
 * Scale vector contents based on the largest of their absolutes.
 */
static int scale_vector(int16_t *dst, const int16_t *vector, int length)
{
    int bits, max = 0;
    int i;

    for (i = 0; i < length; i++)
        max |= FFABS(vector[i]);

    bits= 14 - av_log2_16bit(max);
    bits= FFMAX(bits, 0);

    for (i = 0; i < length; i++)
        dst[i] = vector[i] << bits >> 3;

    return bits - 3;
}

/**
 * Perform inverse quantization of LSP frequencies.
 *
 * @param cur_lsp    the current LSP vector
 * @param prev_lsp   the previous LSP vector
 * @param lsp_index  VQ indices
 * @param bad_frame  bad frame flag
 */
static void inverse_quant(int16_t *cur_lsp, int16_t *prev_lsp,
                          uint8_t *lsp_index, int bad_frame)
{
    int min_dist, pred;
    int i, j, temp, stable;

    /* Check for frame erasure */
    if (!bad_frame) {
        min_dist     = 0x100;
        pred         = 12288;
    } else {
        min_dist     = 0x200;
        pred         = 23552;
        lsp_index[0] = lsp_index[1] = lsp_index[2] = 0;
    }

    /* Get the VQ table entry corresponding to the transmitted index */
    cur_lsp[0] = lsp_band0[lsp_index[0]][0];
    cur_lsp[1] = lsp_band0[lsp_index[0]][1];
    cur_lsp[2] = lsp_band0[lsp_index[0]][2];
    cur_lsp[3] = lsp_band1[lsp_index[1]][0];
    cur_lsp[4] = lsp_band1[lsp_index[1]][1];
    cur_lsp[5] = lsp_band1[lsp_index[1]][2];
    cur_lsp[6] = lsp_band2[lsp_index[2]][0];
    cur_lsp[7] = lsp_band2[lsp_index[2]][1];
    cur_lsp[8] = lsp_band2[lsp_index[2]][2];
    cur_lsp[9] = lsp_band2[lsp_index[2]][3];

    /* Add predicted vector & DC component to the previously quantized vector */
    for (i = 0; i < LPC_ORDER; i++) {
        temp        = ((prev_lsp[i] - dc_lsp[i]) * pred + (1 << 14)) >> 15;
        cur_lsp[i] += dc_lsp[i] + temp;
    }

    for (i = 0; i < LPC_ORDER; i++) {
        cur_lsp[0]             = FFMAX(cur_lsp[0],  0x180);
        cur_lsp[LPC_ORDER - 1] = FFMIN(cur_lsp[LPC_ORDER - 1], 0x7e00);

        /* Stability check */
        for (j = 1; j < LPC_ORDER; j++) {
            temp = min_dist + cur_lsp[j - 1] - cur_lsp[j];
            if (temp > 0) {
                temp >>= 1;
                cur_lsp[j - 1] -= temp;
                cur_lsp[j]     += temp;
            }
        }
        stable = 1;
        for (j = 1; j < LPC_ORDER; j++) {
            temp = cur_lsp[j - 1] + min_dist - cur_lsp[j] - 4;
            if (temp > 0) {
                stable = 0;
                break;
            }
        }
        if (stable)
            break;
    }
    if (!stable)
        memcpy(cur_lsp, prev_lsp, LPC_ORDER * sizeof(*cur_lsp));
}

/**
 * Bitexact implementation of 2ab scaled by 1/2^16.
 *
 * @param a 32 bit multiplicand
 * @param b 16 bit multiplier
 */
#define MULL2(a, b) \
        MULL(a,b,15)

/**
 * Convert LSP frequencies to LPC coefficients.
 *
 * @param lpc buffer for LPC coefficients
 */
static void lsp2lpc(int16_t *lpc)
{
    int f1[LPC_ORDER / 2 + 1];
    int f2[LPC_ORDER / 2 + 1];
    int i, j;

    /* Calculate negative cosine */
    for (j = 0; j < LPC_ORDER; j++) {
        int index     = (lpc[j] >> 7) & 0x1FF;
        int offset    = lpc[j] & 0x7f;
        int temp1     = cos_tab[index] << 16;
        int temp2     = (cos_tab[index + 1] - cos_tab[index]) *
                          ((offset << 8) + 0x80) << 1;

        lpc[j] = -(av_sat_dadd32(1 << 15, temp1 + temp2) >> 16);
    }

    /*
     * Compute sum and difference polynomial coefficients
     * (bitexact alternative to lsp2poly() in lsp.c)
     */
    /* Initialize with values in Q28 */
    f1[0] = 1 << 28;
    f1[1] = (lpc[0] << 14) + (lpc[2] << 14);
    f1[2] = lpc[0] * lpc[2] + (2 << 28);

    f2[0] = 1 << 28;
    f2[1] = (lpc[1] << 14) + (lpc[3] << 14);
    f2[2] = lpc[1] * lpc[3] + (2 << 28);

    /*
     * Calculate and scale the coefficients by 1/2 in
     * each iteration for a final scaling factor of Q25
     */
    for (i = 2; i < LPC_ORDER / 2; i++) {
        f1[i + 1] = f1[i - 1] + MULL2(f1[i], lpc[2 * i]);
        f2[i + 1] = f2[i - 1] + MULL2(f2[i], lpc[2 * i + 1]);

        for (j = i; j >= 2; j--) {
            f1[j] = MULL2(f1[j - 1], lpc[2 * i]) +
                    (f1[j] >> 1) + (f1[j - 2] >> 1);
            f2[j] = MULL2(f2[j - 1], lpc[2 * i + 1]) +
                    (f2[j] >> 1) + (f2[j - 2] >> 1);
        }

        f1[0] >>= 1;
        f2[0] >>= 1;
        f1[1] = ((lpc[2 * i]     << 16 >> i) + f1[1]) >> 1;
        f2[1] = ((lpc[2 * i + 1] << 16 >> i) + f2[1]) >> 1;
    }

    /* Convert polynomial coefficients to LPC coefficients */
    for (i = 0; i < LPC_ORDER / 2; i++) {
        int64_t ff1 = f1[i + 1] + f1[i];
        int64_t ff2 = f2[i + 1] - f2[i];

        lpc[i] = av_clipl_int32(((ff1 + ff2) << 3) + (1 << 15)) >> 16;
        lpc[LPC_ORDER - i - 1] = av_clipl_int32(((ff1 - ff2) << 3) +
                                                (1 << 15)) >> 16;
    }
}

/**
 * Quantize LSP frequencies by interpolation and convert them to
 * the corresponding LPC coefficients.
 *
 * @param lpc      buffer for LPC coefficients
 * @param cur_lsp  the current LSP vector
 * @param prev_lsp the previous LSP vector
 */
static void lsp_interpolate(int16_t *lpc, int16_t *cur_lsp, int16_t *prev_lsp)
{
    int i;
    int16_t *lpc_ptr = lpc;

    /* cur_lsp * 0.25 + prev_lsp * 0.75 */
    ff_acelp_weighted_vector_sum(lpc, cur_lsp, prev_lsp,
                                 4096, 12288, 1 << 13, 14, LPC_ORDER);
    ff_acelp_weighted_vector_sum(lpc + LPC_ORDER, cur_lsp, prev_lsp,
                                 8192, 8192, 1 << 13, 14, LPC_ORDER);
    ff_acelp_weighted_vector_sum(lpc + 2 * LPC_ORDER, cur_lsp, prev_lsp,
                                 12288, 4096, 1 << 13, 14, LPC_ORDER);
    memcpy(lpc + 3 * LPC_ORDER, cur_lsp, LPC_ORDER * sizeof(*lpc));

    for (i = 0; i < SUBFRAMES; i++) {
        lsp2lpc(lpc_ptr);
        lpc_ptr += LPC_ORDER;
    }
}

/**
 * Generate a train of dirac functions with period as pitch lag.
 */
static void gen_dirac_train(int16_t *buf, int pitch_lag)
{
    int16_t vector[SUBFRAME_LEN];
    int i, j;

    memcpy(vector, buf, SUBFRAME_LEN * sizeof(*vector));
    for (i = pitch_lag; i < SUBFRAME_LEN; i += pitch_lag) {
        for (j = 0; j < SUBFRAME_LEN - i; j++)
            buf[i + j] += vector[j];
    }
}

/**
 * Generate fixed codebook excitation vector.
 *
 * @param vector    decoded excitation vector
 * @param subfrm    current subframe
 * @param cur_rate  current bitrate
 * @param pitch_lag closed loop pitch lag
 * @param index     current subframe index
 */
static void gen_fcb_excitation(int16_t *vector, G723_1_Subframe *subfrm,
                               enum Rate cur_rate, int pitch_lag, int index)
{
    int temp, i, j;

    memset(vector, 0, SUBFRAME_LEN * sizeof(*vector));

    if (cur_rate == RATE_6300) {
        if (subfrm->pulse_pos >= max_pos[index])
            return;

        /* Decode amplitudes and positions */
        j = PULSE_MAX - pulses[index];
        temp = subfrm->pulse_pos;
        for (i = 0; i < SUBFRAME_LEN / GRID_SIZE; i++) {
            temp -= combinatorial_table[j][i];
            if (temp >= 0)
                continue;
            temp += combinatorial_table[j++][i];
            if (subfrm->pulse_sign & (1 << (PULSE_MAX - j))) {
                vector[subfrm->grid_index + GRID_SIZE * i] =
                                        -fixed_cb_gain[subfrm->amp_index];
            } else {
                vector[subfrm->grid_index + GRID_SIZE * i] =
                                         fixed_cb_gain[subfrm->amp_index];
            }
            if (j == PULSE_MAX)
                break;
        }
        if (subfrm->dirac_train == 1)
            gen_dirac_train(vector, pitch_lag);
    } else { /* 5300 bps */
        int cb_gain  = fixed_cb_gain[subfrm->amp_index];
        int cb_shift = subfrm->grid_index;
        int cb_sign  = subfrm->pulse_sign;
        int cb_pos   = subfrm->pulse_pos;
        int offset, beta, lag;

        for (i = 0; i < 8; i += 2) {
            offset         = ((cb_pos & 7) << 3) + cb_shift + i;
            vector[offset] = (cb_sign & 1) ? cb_gain : -cb_gain;
            cb_pos  >>= 3;
            cb_sign >>= 1;
        }

        /* Enhance harmonic components */
        lag  = pitch_contrib[subfrm->ad_cb_gain << 1] + pitch_lag +
               subfrm->ad_cb_lag - 1;
        beta = pitch_contrib[(subfrm->ad_cb_gain << 1) + 1];

        if (lag < SUBFRAME_LEN - 2) {
            for (i = lag; i < SUBFRAME_LEN; i++)
                vector[i] += beta * vector[i - lag] >> 15;
        }
    }
}

/**
 * Get delayed contribution from the previous excitation vector.
 */
static void get_residual(int16_t *residual, int16_t *prev_excitation, int lag)
{
    int offset = PITCH_MAX - PITCH_ORDER / 2 - lag;
    int i;

    residual[0] = prev_excitation[offset];
    residual[1] = prev_excitation[offset + 1];

    offset += 2;
    for (i = 2; i < SUBFRAME_LEN + PITCH_ORDER - 1; i++)
        residual[i] = prev_excitation[offset + (i - 2) % lag];
}

static int dot_product(const int16_t *a, const int16_t *b, int length)
{
    int sum = ff_dot_product(a,b,length);
    return av_sat_add32(sum, sum);
}

/**
 * Generate adaptive codebook excitation.
 */
static void gen_acb_excitation(int16_t *vector, int16_t *prev_excitation,
                               int pitch_lag, G723_1_Subframe *subfrm,
                               enum Rate cur_rate)
{
    int16_t residual[SUBFRAME_LEN + PITCH_ORDER - 1];
    const int16_t *cb_ptr;
    int lag = pitch_lag + subfrm->ad_cb_lag - 1;

    int i;
    int sum;

    get_residual(residual, prev_excitation, lag);

    /* Select quantization table */
    if (cur_rate == RATE_6300 && pitch_lag < SUBFRAME_LEN - 2) {
        cb_ptr = adaptive_cb_gain85;
    } else
        cb_ptr = adaptive_cb_gain170;

    /* Calculate adaptive vector */
    cb_ptr += subfrm->ad_cb_gain * 20;
    for (i = 0; i < SUBFRAME_LEN; i++) {
        sum = ff_dot_product(residual + i, cb_ptr, PITCH_ORDER);
        vector[i] = av_sat_dadd32(1 << 15, av_sat_add32(sum, sum)) >> 16;
    }
}

/**
 * Estimate maximum auto-correlation around pitch lag.
 *
 * @param buf       buffer with offset applied
 * @param offset    offset of the excitation vector
 * @param ccr_max   pointer to the maximum auto-correlation
 * @param pitch_lag decoded pitch lag
 * @param length    length of autocorrelation
 * @param dir       forward lag(1) / backward lag(-1)
 */
static int autocorr_max(const int16_t *buf, int offset, int *ccr_max,
                        int pitch_lag, int length, int dir)
{
    int limit, ccr, lag = 0;
    int i;

    pitch_lag = FFMIN(PITCH_MAX - 3, pitch_lag);
    if (dir > 0)
        limit = FFMIN(FRAME_LEN + PITCH_MAX - offset - length, pitch_lag + 3);
    else
        limit = pitch_lag + 3;

    for (i = pitch_lag - 3; i <= limit; i++) {
        ccr = dot_product(buf, buf + dir * i, length);

        if (ccr > *ccr_max) {
            *ccr_max = ccr;
            lag = i;
        }
    }
    return lag;
}

/**
 * Calculate pitch postfilter optimal and scaling gains.
 *
 * @param lag      pitch postfilter forward/backward lag
 * @param ppf      pitch postfilter parameters
 * @param cur_rate current bitrate
 * @param tgt_eng  target energy
 * @param ccr      cross-correlation
 * @param res_eng  residual energy
 */
static void comp_ppf_gains(int lag, PPFParam *ppf, enum Rate cur_rate,
                           int tgt_eng, int ccr, int res_eng)
{
    int pf_residual;     /* square of postfiltered residual */
    int temp1, temp2;

    ppf->index = lag;

    temp1 = tgt_eng * res_eng >> 1;
    temp2 = ccr * ccr << 1;

    if (temp2 > temp1) {
        if (ccr >= res_eng) {
            ppf->opt_gain = ppf_gain_weight[cur_rate];
        } else {
            ppf->opt_gain = (ccr << 15) / res_eng *
                            ppf_gain_weight[cur_rate] >> 15;
        }
        /* pf_res^2 = tgt_eng + 2*ccr*gain + res_eng*gain^2 */
        temp1       = (tgt_eng << 15) + (ccr * ppf->opt_gain << 1);
        temp2       = (ppf->opt_gain * ppf->opt_gain >> 15) * res_eng;
        pf_residual = av_sat_add32(temp1, temp2 + (1 << 15)) >> 16;

        if (tgt_eng >= pf_residual << 1) {
            temp1 = 0x7fff;
        } else {
            temp1 = (tgt_eng << 14) / pf_residual;
        }

        /* scaling_gain = sqrt(tgt_eng/pf_res^2) */
        ppf->sc_gain = square_root(temp1 << 16);
    } else {
        ppf->opt_gain = 0;
        ppf->sc_gain  = 0x7fff;
    }

    ppf->opt_gain = av_clip_int16(ppf->opt_gain * ppf->sc_gain >> 15);
}

/**
 * Calculate pitch postfilter parameters.
 *
 * @param p         the context
 * @param offset    offset of the excitation vector
 * @param pitch_lag decoded pitch lag
 * @param ppf       pitch postfilter parameters
 * @param cur_rate  current bitrate
 */
static void comp_ppf_coeff(G723_1_Context *p, int offset, int pitch_lag,
                           PPFParam *ppf, enum Rate cur_rate)
{

    int16_t scale;
    int i;
    int temp1, temp2;

    /*
     * 0 - target energy
     * 1 - forward cross-correlation
     * 2 - forward residual energy
     * 3 - backward cross-correlation
     * 4 - backward residual energy
     */
    int energy[5] = {0, 0, 0, 0, 0};
    int16_t *buf  = p->audio + LPC_ORDER + offset;
    int fwd_lag   = autocorr_max(buf, offset, &energy[1], pitch_lag,
                                 SUBFRAME_LEN, 1);
    int back_lag  = autocorr_max(buf, offset, &energy[3], pitch_lag,
                                 SUBFRAME_LEN, -1);

    ppf->index    = 0;
    ppf->opt_gain = 0;
    ppf->sc_gain  = 0x7fff;

    /* Case 0, Section 3.6 */
    if (!back_lag && !fwd_lag)
        return;

    /* Compute target energy */
    energy[0] = dot_product(buf, buf, SUBFRAME_LEN);

    /* Compute forward residual energy */
    if (fwd_lag)
        energy[2] = dot_product(buf + fwd_lag, buf + fwd_lag, SUBFRAME_LEN);

    /* Compute backward residual energy */
    if (back_lag)
        energy[4] = dot_product(buf - back_lag, buf - back_lag, SUBFRAME_LEN);

    /* Normalize and shorten */
    temp1 = 0;
    for (i = 0; i < 5; i++)
        temp1 = FFMAX(energy[i], temp1);

    scale = normalize_bits(temp1, 31);
    for (i = 0; i < 5; i++)
        energy[i] = (energy[i] << scale) >> 16;

    if (fwd_lag && !back_lag) {  /* Case 1 */
        comp_ppf_gains(fwd_lag,  ppf, cur_rate, energy[0], energy[1],
                       energy[2]);
    } else if (!fwd_lag) {       /* Case 2 */
        comp_ppf_gains(-back_lag, ppf, cur_rate, energy[0], energy[3],
                       energy[4]);
    } else {                     /* Case 3 */

        /*
         * Select the largest of energy[1]^2/energy[2]
         * and energy[3]^2/energy[4]
         */
        temp1 = energy[4] * ((energy[1] * energy[1] + (1 << 14)) >> 15);
        temp2 = energy[2] * ((energy[3] * energy[3] + (1 << 14)) >> 15);
        if (temp1 >= temp2) {
            comp_ppf_gains(fwd_lag, ppf, cur_rate, energy[0], energy[1],
                           energy[2]);
        } else {
            comp_ppf_gains(-back_lag, ppf, cur_rate, energy[0], energy[3],
                           energy[4]);
        }
    }
}

/**
 * Classify frames as voiced/unvoiced.
 *
 * @param p         the context
 * @param pitch_lag decoded pitch_lag
 * @param exc_eng   excitation energy estimation
 * @param scale     scaling factor of exc_eng
 *
 * @return residual interpolation index if voiced, 0 otherwise
 */
static int comp_interp_index(G723_1_Context *p, int pitch_lag,
                             int *exc_eng, int *scale)
{
    int offset = PITCH_MAX + 2 * SUBFRAME_LEN;
    int16_t *buf = p->audio + LPC_ORDER;

    int index, ccr, tgt_eng, best_eng, temp;

    *scale = scale_vector(buf, p->excitation, FRAME_LEN + PITCH_MAX);
    buf   += offset;

    /* Compute maximum backward cross-correlation */
    ccr   = 0;
    index = autocorr_max(buf, offset, &ccr, pitch_lag, SUBFRAME_LEN * 2, -1);
    ccr   = av_sat_add32(ccr, 1 << 15) >> 16;

    /* Compute target energy */
    tgt_eng  = dot_product(buf, buf, SUBFRAME_LEN * 2);
    *exc_eng = av_sat_add32(tgt_eng, 1 << 15) >> 16;

    if (ccr <= 0)
        return 0;

    /* Compute best energy */
    best_eng = dot_product(buf - index, buf - index, SUBFRAME_LEN * 2);
    best_eng = av_sat_add32(best_eng, 1 << 15) >> 16;

    temp = best_eng * *exc_eng >> 3;

    if (temp < ccr * ccr) {
        return index;
    } else
        return 0;
}

/**
 * Peform residual interpolation based on frame classification.
 *
 * @param buf   decoded excitation vector
 * @param out   output vector
 * @param lag   decoded pitch lag
 * @param gain  interpolated gain
 * @param rseed seed for random number generator
 */
static void residual_interp(int16_t *buf, int16_t *out, int lag,
                            int gain, int *rseed)
{
    int i;
    if (lag) { /* Voiced */
        int16_t *vector_ptr = buf + PITCH_MAX;
        /* Attenuate */
        for (i = 0; i < lag; i++)
            out[i] = vector_ptr[i - lag] * 3 >> 2;
        av_memcpy_backptr((uint8_t*)(out + lag), lag * sizeof(*out),
                          (FRAME_LEN - lag) * sizeof(*out));
    } else {  /* Unvoiced */
        for (i = 0; i < FRAME_LEN; i++) {
            *rseed = *rseed * 521 + 259;
            out[i] = gain * *rseed >> 15;
        }
        memset(buf, 0, (FRAME_LEN + PITCH_MAX) * sizeof(*buf));
    }
}

/**
 * Perform IIR filtering.
 *
 * @param fir_coef FIR coefficients
 * @param iir_coef IIR coefficients
 * @param src      source vector
 * @param dest     destination vector
 * @param width    width of the output, 16 bits(0) / 32 bits(1)
 */
#define iir_filter(fir_coef, iir_coef, src, dest, width)\
{\
    int m, n;\
    int res_shift = 16 & ~-(width);\
    int in_shift  = 16 - res_shift;\
\
    for (m = 0; m < SUBFRAME_LEN; m++) {\
        int64_t filter = 0;\
        for (n = 1; n <= LPC_ORDER; n++) {\
            filter -= (fir_coef)[n - 1] * (src)[m - n] -\
                      (iir_coef)[n - 1] * ((dest)[m - n] >> in_shift);\
        }\
\
        (dest)[m] = av_clipl_int32(((src)[m] << 16) + (filter << 3) +\
                                   (1 << 15)) >> res_shift;\
    }\
}

/**
 * Adjust gain of postfiltered signal.
 *
 * @param p      the context
 * @param buf    postfiltered output vector
 * @param energy input energy coefficient
 */
static void gain_scale(G723_1_Context *p, int16_t * buf, int energy)
{
    int num, denom, gain, bits1, bits2;
    int i;

    num   = energy;
    denom = 0;
    for (i = 0; i < SUBFRAME_LEN; i++) {
        int temp = buf[i] >> 2;
        temp *= temp;
        denom = av_sat_dadd32(denom, temp);
    }

    if (num && denom) {
        bits1   = normalize_bits(num,   31);
        bits2   = normalize_bits(denom, 31);
        num     = num << bits1 >> 1;
        denom <<= bits2;

        bits2 = 5 + bits1 - bits2;
        bits2 = FFMAX(0, bits2);

        gain = (num >> 1) / (denom >> 16);
        gain = square_root(gain << 16 >> bits2);
    } else {
        gain = 1 << 12;
    }

    for (i = 0; i < SUBFRAME_LEN; i++) {
        p->pf_gain = (15 * p->pf_gain + gain + (1 << 3)) >> 4;
        buf[i]     = av_clip_int16((buf[i] * (p->pf_gain + (p->pf_gain >> 4)) +
                                   (1 << 10)) >> 11);
    }
}

/**
 * Perform formant filtering.
 *
 * @param p   the context
 * @param lpc quantized lpc coefficients
 * @param buf input buffer
 * @param dst output buffer
 */
static void formant_postfilter(G723_1_Context *p, int16_t *lpc,
                               int16_t *buf, int16_t *dst)
{
    int16_t filter_coef[2][LPC_ORDER];
    int filter_signal[LPC_ORDER + FRAME_LEN], *signal_ptr;
    int i, j, k;

    memcpy(buf, p->fir_mem, LPC_ORDER * sizeof(*buf));
    memcpy(filter_signal, p->iir_mem, LPC_ORDER * sizeof(*filter_signal));

    for (i = LPC_ORDER, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++) {
        for (k = 0; k < LPC_ORDER; k++) {
            filter_coef[0][k] = (-lpc[k] * postfilter_tbl[0][k] +
                                 (1 << 14)) >> 15;
            filter_coef[1][k] = (-lpc[k] * postfilter_tbl[1][k] +
                                 (1 << 14)) >> 15;
        }
        iir_filter(filter_coef[0], filter_coef[1], buf + i,
                   filter_signal + i, 1);
        lpc += LPC_ORDER;
    }

    memcpy(p->fir_mem, buf + FRAME_LEN, LPC_ORDER * sizeof(int16_t));
    memcpy(p->iir_mem, filter_signal + FRAME_LEN, LPC_ORDER * sizeof(int));

    buf += LPC_ORDER;
    signal_ptr = filter_signal + LPC_ORDER;
    for (i = 0; i < SUBFRAMES; i++) {
        int temp;
        int auto_corr[2];
        int scale, energy;

        /* Normalize */
        scale = scale_vector(dst, buf, SUBFRAME_LEN);

        /* Compute auto correlation coefficients */
        auto_corr[0] = dot_product(dst, dst + 1, SUBFRAME_LEN - 1);
        auto_corr[1] = dot_product(dst, dst,     SUBFRAME_LEN);

        /* Compute reflection coefficient */
        temp = auto_corr[1] >> 16;
        if (temp) {
            temp = (auto_corr[0] >> 2) / temp;
        }
        p->reflection_coef = (3 * p->reflection_coef + temp + 2) >> 2;
        temp = -p->reflection_coef >> 1 & ~3;

        /* Compensation filter */
        for (j = 0; j < SUBFRAME_LEN; j++) {
            dst[j] = av_sat_dadd32(signal_ptr[j],
                                   (signal_ptr[j - 1] >> 16) * temp) >> 16;
        }

        /* Compute normalized signal energy */
        temp = 2 * scale + 4;
        if (temp < 0) {
            energy = av_clipl_int32((int64_t)auto_corr[1] << -temp);
        } else
            energy = auto_corr[1] >> temp;

        gain_scale(p, dst, energy);

        buf        += SUBFRAME_LEN;
        signal_ptr += SUBFRAME_LEN;
        dst        += SUBFRAME_LEN;
    }
}

static int sid_gain_to_lsp_index(int gain)
{
    if (gain < 0x10)
        return gain << 6;
    else if (gain < 0x20)
        return gain - 8 << 7;
    else
        return gain - 20 << 8;
}

static inline int cng_rand(int *state, int base)
{
    *state = (*state * 521 + 259) & 0xFFFF;
    return (*state & 0x7FFF) * base >> 15;
}

static int estimate_sid_gain(G723_1_Context *p)
{
    int i, shift, seg, seg2, t, val, val_add, x, y;

    shift = 16 - p->cur_gain * 2;
    if (shift > 0)
        t = p->sid_gain << shift;
    else
        t = p->sid_gain >> -shift;
    x = t * cng_filt[0] >> 16;

    if (x >= cng_bseg[2])
        return 0x3F;

    if (x >= cng_bseg[1]) {
        shift = 4;
        seg   = 3;
    } else {
        shift = 3;
        seg   = (x >= cng_bseg[0]);
    }
    seg2 = FFMIN(seg, 3);

    val     = 1 << shift;
    val_add = val >> 1;
    for (i = 0; i < shift; i++) {
        t = seg * 32 + (val << seg2);
        t *= t;
        if (x >= t)
            val += val_add;
        else
            val -= val_add;
        val_add >>= 1;
    }

    t = seg * 32 + (val << seg2);
    y = t * t - x;
    if (y <= 0) {
        t = seg * 32 + (val + 1 << seg2);
        t = t * t - x;
        val = (seg2 - 1 << 4) + val;
        if (t >= y)
            val++;
    } else {
        t = seg * 32 + (val - 1 << seg2);
        t = t * t - x;
        val = (seg2 - 1 << 4) + val;
        if (t >= y)
            val--;
    }

    return val;
}

static void generate_noise(G723_1_Context *p)
{
    int i, j, idx, t;
    int off[SUBFRAMES];
    int signs[SUBFRAMES / 2 * 11], pos[SUBFRAMES / 2 * 11];
    int tmp[SUBFRAME_LEN * 2];
    int16_t *vector_ptr;
    int64_t sum;
    int b0, c, delta, x, shift;

    p->pitch_lag[0] = cng_rand(&p->cng_random_seed, 21) + 123;
    p->pitch_lag[1] = cng_rand(&p->cng_random_seed, 19) + 123;

    for (i = 0; i < SUBFRAMES; i++) {
        p->subframe[i].ad_cb_gain = cng_rand(&p->cng_random_seed, 50) + 1;
        p->subframe[i].ad_cb_lag  = cng_adaptive_cb_lag[i];
    }

    for (i = 0; i < SUBFRAMES / 2; i++) {
        t = cng_rand(&p->cng_random_seed, 1 << 13);
        off[i * 2]     =   t       & 1;
        off[i * 2 + 1] = ((t >> 1) & 1) + SUBFRAME_LEN;
        t >>= 2;
        for (j = 0; j < 11; j++) {
            signs[i * 11 + j] = (t & 1) * 2 - 1 << 14;
            t >>= 1;
        }
    }

    idx = 0;
    for (i = 0; i < SUBFRAMES; i++) {
        for (j = 0; j < SUBFRAME_LEN / 2; j++)
            tmp[j] = j;
        t = SUBFRAME_LEN / 2;
        for (j = 0; j < pulses[i]; j++, idx++) {
            int idx2 = cng_rand(&p->cng_random_seed, t);

            pos[idx]  = tmp[idx2] * 2 + off[i];
            tmp[idx2] = tmp[--t];
        }
    }

    vector_ptr = p->audio + LPC_ORDER;
    memcpy(vector_ptr, p->prev_excitation,
           PITCH_MAX * sizeof(*p->excitation));
    for (i = 0; i < SUBFRAMES; i += 2) {
        gen_acb_excitation(vector_ptr, vector_ptr,
                           p->pitch_lag[i >> 1], &p->subframe[i],
                           p->cur_rate);
        gen_acb_excitation(vector_ptr + SUBFRAME_LEN,
                           vector_ptr + SUBFRAME_LEN,
                           p->pitch_lag[i >> 1], &p->subframe[i + 1],
                           p->cur_rate);

        t = 0;
        for (j = 0; j < SUBFRAME_LEN * 2; j++)
            t |= FFABS(vector_ptr[j]);
        t = FFMIN(t, 0x7FFF);
        if (!t) {
            shift = 0;
        } else {
            shift = -10 + av_log2(t);
            if (shift < -2)
                shift = -2;
        }
        sum = 0;
        if (shift < 0) {
           for (j = 0; j < SUBFRAME_LEN * 2; j++) {
               t      = vector_ptr[j] << -shift;
               sum   += t * t;
               tmp[j] = t;
           }
        } else {
           for (j = 0; j < SUBFRAME_LEN * 2; j++) {
               t      = vector_ptr[j] >> shift;
               sum   += t * t;
               tmp[j] = t;
           }
        }

        b0 = 0;
        for (j = 0; j < 11; j++)
            b0 += tmp[pos[(i / 2) * 11 + j]] * signs[(i / 2) * 11 + j];
        b0 = b0 * 2 * 2979LL + (1 << 29) >> 30; // approximated division by 11

        c = p->cur_gain * (p->cur_gain * SUBFRAME_LEN >> 5);
        if (shift * 2 + 3 >= 0)
            c >>= shift * 2 + 3;
        else
            c <<= -(shift * 2 + 3);
        c = (av_clipl_int32(sum << 1) - c) * 2979LL >> 15;

        delta = b0 * b0 * 2 - c;
        if (delta <= 0) {
            x = -b0;
        } else {
            delta = square_root(delta);
            x     = delta - b0;
            t     = delta + b0;
            if (FFABS(t) < FFABS(x))
                x = -t;
        }
        shift++;
        if (shift < 0)
           x >>= -shift;
        else
           x <<= shift;
        x = av_clip(x, -10000, 10000);

        for (j = 0; j < 11; j++) {
            idx = (i / 2) * 11 + j;
            vector_ptr[pos[idx]] = av_clip_int16(vector_ptr[pos[idx]] +
                                                 (x * signs[idx] >> 15));
        }

        /* copy decoded data to serve as a history for the next decoded subframes */
        memcpy(vector_ptr + PITCH_MAX, vector_ptr,
               sizeof(*vector_ptr) * SUBFRAME_LEN * 2);
        vector_ptr += SUBFRAME_LEN * 2;
    }
    /* Save the excitation for the next frame */
    memcpy(p->prev_excitation, p->audio + LPC_ORDER + FRAME_LEN,
           PITCH_MAX * sizeof(*p->excitation));
}

static int g723_1_decode_frame(AVCodecContext *avctx, void *data,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    G723_1_Context *p  = avctx->priv_data;
    AVFrame *frame     = data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int dec_mode       = buf[0] & 3;

    PPFParam ppf[SUBFRAMES];
    int16_t cur_lsp[LPC_ORDER];
    int16_t lpc[SUBFRAMES * LPC_ORDER];
    int16_t acb_vector[SUBFRAME_LEN];
    int16_t *out;
    int bad_frame = 0, i, j, ret;
    int16_t *audio = p->audio;

    if (buf_size < frame_size[dec_mode]) {
        if (buf_size)
            av_log(avctx, AV_LOG_WARNING,
                   "Expected %d bytes, got %d - skipping packet\n",
                   frame_size[dec_mode], buf_size);
        *got_frame_ptr = 0;
        return buf_size;
    }

    if (unpack_bitstream(p, buf, buf_size) < 0) {
        bad_frame = 1;
        if (p->past_frame_type == ACTIVE_FRAME)
            p->cur_frame_type = ACTIVE_FRAME;
        else
            p->cur_frame_type = UNTRANSMITTED_FRAME;
    }

    frame->nb_samples = FRAME_LEN;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    out = (int16_t *)frame->data[0];

    if (p->cur_frame_type == ACTIVE_FRAME) {
        if (!bad_frame)
            p->erased_frames = 0;
        else if (p->erased_frames != 3)
            p->erased_frames++;

        inverse_quant(cur_lsp, p->prev_lsp, p->lsp_index, bad_frame);
        lsp_interpolate(lpc, cur_lsp, p->prev_lsp);

        /* Save the lsp_vector for the next frame */
        memcpy(p->prev_lsp, cur_lsp, LPC_ORDER * sizeof(*p->prev_lsp));

        /* Generate the excitation for the frame */
        memcpy(p->excitation, p->prev_excitation,
               PITCH_MAX * sizeof(*p->excitation));
        if (!p->erased_frames) {
            int16_t *vector_ptr = p->excitation + PITCH_MAX;

            /* Update interpolation gain memory */
            p->interp_gain = fixed_cb_gain[(p->subframe[2].amp_index +
                                            p->subframe[3].amp_index) >> 1];
            for (i = 0; i < SUBFRAMES; i++) {
                gen_fcb_excitation(vector_ptr, &p->subframe[i], p->cur_rate,
                                   p->pitch_lag[i >> 1], i);
                gen_acb_excitation(acb_vector, &p->excitation[SUBFRAME_LEN * i],
                                   p->pitch_lag[i >> 1], &p->subframe[i],
                                   p->cur_rate);
                /* Get the total excitation */
                for (j = 0; j < SUBFRAME_LEN; j++) {
                    int v = av_clip_int16(vector_ptr[j] << 1);
                    vector_ptr[j] = av_clip_int16(v + acb_vector[j]);
                }
                vector_ptr += SUBFRAME_LEN;
            }

            vector_ptr = p->excitation + PITCH_MAX;

            p->interp_index = comp_interp_index(p, p->pitch_lag[1],
                                                &p->sid_gain, &p->cur_gain);

            /* Peform pitch postfiltering */
            if (p->postfilter) {
                i = PITCH_MAX;
                for (j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
                    comp_ppf_coeff(p, i, p->pitch_lag[j >> 1],
                                   ppf + j, p->cur_rate);

                for (i = 0, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
                    ff_acelp_weighted_vector_sum(p->audio + LPC_ORDER + i,
                                                 vector_ptr + i,
                                                 vector_ptr + i + ppf[j].index,
                                                 ppf[j].sc_gain,
                                                 ppf[j].opt_gain,
                                                 1 << 14, 15, SUBFRAME_LEN);
            } else {
                audio = vector_ptr - LPC_ORDER;
            }

            /* Save the excitation for the next frame */
            memcpy(p->prev_excitation, p->excitation + FRAME_LEN,
                   PITCH_MAX * sizeof(*p->excitation));
        } else {
            p->interp_gain = (p->interp_gain * 3 + 2) >> 2;
            if (p->erased_frames == 3) {
                /* Mute output */
                memset(p->excitation, 0,
                       (FRAME_LEN + PITCH_MAX) * sizeof(*p->excitation));
                memset(p->prev_excitation, 0,
                       PITCH_MAX * sizeof(*p->excitation));
                memset(frame->data[0], 0,
                       (FRAME_LEN + LPC_ORDER) * sizeof(int16_t));
            } else {
                int16_t *buf = p->audio + LPC_ORDER;

                /* Regenerate frame */
                residual_interp(p->excitation, buf, p->interp_index,
                                p->interp_gain, &p->random_seed);

                /* Save the excitation for the next frame */
                memcpy(p->prev_excitation, buf + (FRAME_LEN - PITCH_MAX),
                       PITCH_MAX * sizeof(*p->excitation));
            }
        }
        p->cng_random_seed = CNG_RANDOM_SEED;
    } else {
        if (p->cur_frame_type == SID_FRAME) {
            p->sid_gain = sid_gain_to_lsp_index(p->subframe[0].amp_index);
            inverse_quant(p->sid_lsp, p->prev_lsp, p->lsp_index, 0);
        } else if (p->past_frame_type == ACTIVE_FRAME) {
            p->sid_gain = estimate_sid_gain(p);
        }

        if (p->past_frame_type == ACTIVE_FRAME)
            p->cur_gain = p->sid_gain;
        else
            p->cur_gain = (p->cur_gain * 7 + p->sid_gain) >> 3;
        generate_noise(p);
        lsp_interpolate(lpc, p->sid_lsp, p->prev_lsp);
        /* Save the lsp_vector for the next frame */
        memcpy(p->prev_lsp, p->sid_lsp, LPC_ORDER * sizeof(*p->prev_lsp));
    }

    p->past_frame_type = p->cur_frame_type;

    memcpy(p->audio, p->synth_mem, LPC_ORDER * sizeof(*p->audio));
    for (i = LPC_ORDER, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
        ff_celp_lp_synthesis_filter(p->audio + i, &lpc[j * LPC_ORDER],
                                    audio + i, SUBFRAME_LEN, LPC_ORDER,
                                    0, 1, 1 << 12);
    memcpy(p->synth_mem, p->audio + FRAME_LEN, LPC_ORDER * sizeof(*p->audio));

    if (p->postfilter) {
        formant_postfilter(p, lpc, p->audio, out);
    } else { // if output is not postfiltered it should be scaled by 2
        for (i = 0; i < FRAME_LEN; i++)
            out[i] = av_clip_int16(p->audio[LPC_ORDER + i] << 1);
    }

    *got_frame_ptr = 1;

    return frame_size[dec_mode];
}

#define OFFSET(x) offsetof(G723_1_Context, x)
#define AD     AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "postfilter", "postfilter on/off", OFFSET(postfilter), AV_OPT_TYPE_INT,
      { .i64 = 1 }, 0, 1, AD },
    { NULL }
};


static const AVClass g723_1dec_class = {
    .class_name = "G.723.1 decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_g723_1_decoder = {
    .name           = "g723_1",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_G723_1,
    .priv_data_size = sizeof(G723_1_Context),
    .init           = g723_1_decode_init,
    .decode         = g723_1_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("G.723.1"),
    .capabilities   = CODEC_CAP_SUBFRAMES | CODEC_CAP_DR1,
    .priv_class     = &g723_1dec_class,
};

#if CONFIG_G723_1_ENCODER
#define BITSTREAM_WRITER_LE
#include "put_bits.h"

static av_cold int g723_1_encode_init(AVCodecContext *avctx)
{
    G723_1_Context *p = avctx->priv_data;

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return -1;
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->bit_rate == 6300) {
        p->cur_rate = RATE_6300;
    } else if (avctx->bit_rate == 5300) {
        av_log(avctx, AV_LOG_ERROR, "Bitrate not supported yet, use 6.3k\n");
        return AVERROR_PATCHWELCOME;
    } else {
        av_log(avctx, AV_LOG_ERROR,
               "Bitrate not supported, use 6.3k\n");
        return AVERROR(EINVAL);
    }
    avctx->frame_size = 240;
    memcpy(p->prev_lsp, dc_lsp, LPC_ORDER * sizeof(int16_t));

    return 0;
}

/**
 * Remove DC component from the input signal.
 *
 * @param buf input signal
 * @param fir zero memory
 * @param iir pole memory
 */
static void highpass_filter(int16_t *buf, int16_t *fir, int *iir)
{
    int i;
    for (i = 0; i < FRAME_LEN; i++) {
        *iir   = (buf[i] << 15) + ((-*fir) << 15) + MULL2(*iir, 0x7f00);
        *fir   = buf[i];
        buf[i] = av_clipl_int32((int64_t)*iir + (1 << 15)) >> 16;
    }
}

/**
 * Estimate autocorrelation of the input vector.
 *
 * @param buf      input buffer
 * @param autocorr autocorrelation coefficients vector
 */
static void comp_autocorr(int16_t *buf, int16_t *autocorr)
{
    int i, scale, temp;
    int16_t vector[LPC_FRAME];

    scale_vector(vector, buf, LPC_FRAME);

    /* Apply the Hamming window */
    for (i = 0; i < LPC_FRAME; i++)
        vector[i] = (vector[i] * hamming_window[i] + (1 << 14)) >> 15;

    /* Compute the first autocorrelation coefficient */
    temp = ff_dot_product(vector, vector, LPC_FRAME);

    /* Apply a white noise correlation factor of (1025/1024) */
    temp += temp >> 10;

    /* Normalize */
    scale = normalize_bits_int32(temp);
    autocorr[0] = av_clipl_int32((int64_t)(temp << scale) +
                                 (1 << 15)) >> 16;

    /* Compute the remaining coefficients */
    if (!autocorr[0]) {
        memset(autocorr + 1, 0, LPC_ORDER * sizeof(int16_t));
    } else {
        for (i = 1; i <= LPC_ORDER; i++) {
           temp = ff_dot_product(vector, vector + i, LPC_FRAME - i);
           temp = MULL2((temp << scale), binomial_window[i - 1]);
           autocorr[i] = av_clipl_int32((int64_t)temp + (1 << 15)) >> 16;
        }
    }
}

/**
 * Use Levinson-Durbin recursion to compute LPC coefficients from
 * autocorrelation values.
 *
 * @param lpc      LPC coefficients vector
 * @param autocorr autocorrelation coefficients vector
 * @param error    prediction error
 */
static void levinson_durbin(int16_t *lpc, int16_t *autocorr, int16_t error)
{
    int16_t vector[LPC_ORDER];
    int16_t partial_corr;
    int i, j, temp;

    memset(lpc, 0, LPC_ORDER * sizeof(int16_t));

    for (i = 0; i < LPC_ORDER; i++) {
        /* Compute the partial correlation coefficient */
        temp = 0;
        for (j = 0; j < i; j++)
            temp -= lpc[j] * autocorr[i - j - 1];
        temp = ((autocorr[i] << 13) + temp) << 3;

        if (FFABS(temp) >= (error << 16))
            break;

        partial_corr = temp / (error << 1);

        lpc[i] = av_clipl_int32((int64_t)(partial_corr << 14) +
                                (1 << 15)) >> 16;

        /* Update the prediction error */
        temp  = MULL2(temp, partial_corr);
        error = av_clipl_int32((int64_t)(error << 16) - temp +
                                (1 << 15)) >> 16;

        memcpy(vector, lpc, i * sizeof(int16_t));
        for (j = 0; j < i; j++) {
            temp = partial_corr * vector[i - j - 1] << 1;
            lpc[j] = av_clipl_int32((int64_t)(lpc[j] << 16) - temp +
                                    (1 << 15)) >> 16;
        }
    }
}

/**
 * Calculate LPC coefficients for the current frame.
 *
 * @param buf       current frame
 * @param prev_data 2 trailing subframes of the previous frame
 * @param lpc       LPC coefficients vector
 */
static void comp_lpc_coeff(int16_t *buf, int16_t *lpc)
{
    int16_t autocorr[(LPC_ORDER + 1) * SUBFRAMES];
    int16_t *autocorr_ptr = autocorr;
    int16_t *lpc_ptr      = lpc;
    int i, j;

    for (i = 0, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++) {
        comp_autocorr(buf + i, autocorr_ptr);
        levinson_durbin(lpc_ptr, autocorr_ptr + 1, autocorr_ptr[0]);

        lpc_ptr += LPC_ORDER;
        autocorr_ptr += LPC_ORDER + 1;
    }
}

static void lpc2lsp(int16_t *lpc, int16_t *prev_lsp, int16_t *lsp)
{
    int f[LPC_ORDER + 2]; ///< coefficients of the sum and difference
                          ///< polynomials (F1, F2) ordered as
                          ///< f1[0], f2[0], ...., f1[5], f2[5]

    int max, shift, cur_val, prev_val, count, p;
    int i, j;
    int64_t temp;

    /* Initialize f1[0] and f2[0] to 1 in Q25 */
    for (i = 0; i < LPC_ORDER; i++)
        lsp[i] = (lpc[i] * bandwidth_expand[i] + (1 << 14)) >> 15;

    /* Apply bandwidth expansion on the LPC coefficients */
    f[0] = f[1] = 1 << 25;

    /* Compute the remaining coefficients */
    for (i = 0; i < LPC_ORDER / 2; i++) {
        /* f1 */
        f[2 * i + 2] = -f[2 * i] - ((lsp[i] + lsp[LPC_ORDER - 1 - i]) << 12);
        /* f2 */
        f[2 * i + 3] = f[2 * i + 1] - ((lsp[i] - lsp[LPC_ORDER - 1 - i]) << 12);
    }

    /* Divide f1[5] and f2[5] by 2 for use in polynomial evaluation */
    f[LPC_ORDER] >>= 1;
    f[LPC_ORDER + 1] >>= 1;

    /* Normalize and shorten */
    max = FFABS(f[0]);
    for (i = 1; i < LPC_ORDER + 2; i++)
        max = FFMAX(max, FFABS(f[i]));

    shift = normalize_bits_int32(max);

    for (i = 0; i < LPC_ORDER + 2; i++)
        f[i] = av_clipl_int32((int64_t)(f[i] << shift) + (1 << 15)) >> 16;

    /**
     * Evaluate F1 and F2 at uniform intervals of pi/256 along the
     * unit circle and check for zero crossings.
     */
    p    = 0;
    temp = 0;
    for (i = 0; i <= LPC_ORDER / 2; i++)
        temp += f[2 * i] * cos_tab[0];
    prev_val = av_clipl_int32(temp << 1);
    count    = 0;
    for ( i = 1; i < COS_TBL_SIZE / 2; i++) {
        /* Evaluate */
        temp = 0;
        for (j = 0; j <= LPC_ORDER / 2; j++)
            temp += f[LPC_ORDER - 2 * j + p] * cos_tab[i * j % COS_TBL_SIZE];
        cur_val = av_clipl_int32(temp << 1);

        /* Check for sign change, indicating a zero crossing */
        if ((cur_val ^ prev_val) < 0) {
            int abs_cur  = FFABS(cur_val);
            int abs_prev = FFABS(prev_val);
            int sum      = abs_cur + abs_prev;

            shift        = normalize_bits_int32(sum);
            sum          <<= shift;
            abs_prev     = abs_prev << shift >> 8;
            lsp[count++] = ((i - 1) << 7) + (abs_prev >> 1) / (sum >> 16);

            if (count == LPC_ORDER)
                break;

            /* Switch between sum and difference polynomials */
            p ^= 1;

            /* Evaluate */
            temp = 0;
            for (j = 0; j <= LPC_ORDER / 2; j++){
                temp += f[LPC_ORDER - 2 * j + p] *
                        cos_tab[i * j % COS_TBL_SIZE];
            }
            cur_val = av_clipl_int32(temp<<1);
        }
        prev_val = cur_val;
    }

    if (count != LPC_ORDER)
        memcpy(lsp, prev_lsp, LPC_ORDER * sizeof(int16_t));
}

/**
 * Quantize the current LSP subvector.
 *
 * @param num    band number
 * @param offset offset of the current subvector in an LPC_ORDER vector
 * @param size   size of the current subvector
 */
#define get_index(num, offset, size) \
{\
    int error, max = -1;\
    int16_t temp[4];\
    int i, j;\
    for (i = 0; i < LSP_CB_SIZE; i++) {\
        for (j = 0; j < size; j++){\
            temp[j] = (weight[j + (offset)] * lsp_band##num[i][j] +\
                      (1 << 14)) >> 15;\
        }\
        error =  dot_product(lsp + (offset), temp, size) << 1;\
        error -= dot_product(lsp_band##num[i], temp, size);\
        if (error > max) {\
            max = error;\
            lsp_index[num] = i;\
        }\
    }\
}

/**
 * Vector quantize the LSP frequencies.
 *
 * @param lsp      the current lsp vector
 * @param prev_lsp the previous lsp vector
 */
static void lsp_quantize(uint8_t *lsp_index, int16_t *lsp, int16_t *prev_lsp)
{
    int16_t weight[LPC_ORDER];
    int16_t min, max;
    int shift, i;

    /* Calculate the VQ weighting vector */
    weight[0] = (1 << 20) / (lsp[1] - lsp[0]);
    weight[LPC_ORDER - 1] = (1 << 20) /
                            (lsp[LPC_ORDER - 1] - lsp[LPC_ORDER - 2]);

    for (i = 1; i < LPC_ORDER - 1; i++) {
        min  = FFMIN(lsp[i] - lsp[i - 1], lsp[i + 1] - lsp[i]);
        if (min > 0x20)
            weight[i] = (1 << 20) / min;
        else
            weight[i] = INT16_MAX;
    }

    /* Normalize */
    max = 0;
    for (i = 0; i < LPC_ORDER; i++)
        max = FFMAX(weight[i], max);

    shift = normalize_bits_int16(max);
    for (i = 0; i < LPC_ORDER; i++) {
        weight[i] <<= shift;
    }

    /* Compute the VQ target vector */
    for (i = 0; i < LPC_ORDER; i++) {
        lsp[i] -= dc_lsp[i] +
                  (((prev_lsp[i] - dc_lsp[i]) * 12288 + (1 << 14)) >> 15);
    }

    get_index(0, 0, 3);
    get_index(1, 3, 3);
    get_index(2, 6, 4);
}

/**
 * Apply the formant perceptual weighting filter.
 *
 * @param flt_coef filter coefficients
 * @param unq_lpc  unquantized lpc vector
 */
static void perceptual_filter(G723_1_Context *p, int16_t *flt_coef,
                              int16_t *unq_lpc, int16_t *buf)
{
    int16_t vector[FRAME_LEN + LPC_ORDER];
    int i, j, k, l = 0;

    memcpy(buf, p->iir_mem, sizeof(int16_t) * LPC_ORDER);
    memcpy(vector, p->fir_mem, sizeof(int16_t) * LPC_ORDER);
    memcpy(vector + LPC_ORDER, buf + LPC_ORDER, sizeof(int16_t) * FRAME_LEN);

    for (i = LPC_ORDER, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++) {
        for (k = 0; k < LPC_ORDER; k++) {
            flt_coef[k + 2 * l] = (unq_lpc[k + l] * percept_flt_tbl[0][k] +
                                  (1 << 14)) >> 15;
            flt_coef[k + 2 * l + LPC_ORDER] = (unq_lpc[k + l] *
                                             percept_flt_tbl[1][k] +
                                             (1 << 14)) >> 15;
        }
        iir_filter(flt_coef + 2 * l, flt_coef + 2 * l + LPC_ORDER, vector + i,
                   buf + i, 0);
        l += LPC_ORDER;
    }
    memcpy(p->iir_mem, buf + FRAME_LEN, sizeof(int16_t) * LPC_ORDER);
    memcpy(p->fir_mem, vector + FRAME_LEN, sizeof(int16_t) * LPC_ORDER);
}

/**
 * Estimate the open loop pitch period.
 *
 * @param buf   perceptually weighted speech
 * @param start estimation is carried out from this position
 */
static int estimate_pitch(int16_t *buf, int start)
{
    int max_exp = 32;
    int max_ccr = 0x4000;
    int max_eng = 0x7fff;
    int index   = PITCH_MIN;
    int offset  = start - PITCH_MIN + 1;

    int ccr, eng, orig_eng, ccr_eng, exp;
    int diff, temp;

    int i;

    orig_eng = ff_dot_product(buf + offset, buf + offset, HALF_FRAME_LEN);

    for (i = PITCH_MIN; i <= PITCH_MAX - 3; i++) {
        offset--;

        /* Update energy and compute correlation */
        orig_eng += buf[offset] * buf[offset] -
                    buf[offset + HALF_FRAME_LEN] * buf[offset + HALF_FRAME_LEN];
        ccr      =  ff_dot_product(buf + start, buf + offset, HALF_FRAME_LEN);
        if (ccr <= 0)
            continue;

        /* Split into mantissa and exponent to maintain precision */
        exp  =   normalize_bits_int32(ccr);
        ccr  =   av_clipl_int32((int64_t)(ccr << exp) + (1 << 15)) >> 16;
        exp  <<= 1;
        ccr  *=  ccr;
        temp =   normalize_bits_int32(ccr);
        ccr  =   ccr << temp >> 16;
        exp  +=  temp;

        temp =   normalize_bits_int32(orig_eng);
        eng  =   av_clipl_int32((int64_t)(orig_eng << temp) + (1 << 15)) >> 16;
        exp  -=  temp;

        if (ccr >= eng) {
            exp--;
            ccr >>= 1;
        }
        if (exp > max_exp)
            continue;

        if (exp + 1 < max_exp)
            goto update;

        /* Equalize exponents before comparison */
        if (exp + 1 == max_exp)
            temp = max_ccr >> 1;
        else
            temp = max_ccr;
        ccr_eng = ccr * max_eng;
        diff    = ccr_eng - eng * temp;
        if (diff > 0 && (i - index < PITCH_MIN || diff > ccr_eng >> 2)) {
update:
            index   = i;
            max_exp = exp;
            max_ccr = ccr;
            max_eng = eng;
        }
    }
    return index;
}

/**
 * Compute harmonic noise filter parameters.
 *
 * @param buf       perceptually weighted speech
 * @param pitch_lag open loop pitch period
 * @param hf        harmonic filter parameters
 */
static void comp_harmonic_coeff(int16_t *buf, int16_t pitch_lag, HFParam *hf)
{
    int ccr, eng, max_ccr, max_eng;
    int exp, max, diff;
    int energy[15];
    int i, j;

    for (i = 0, j = pitch_lag - 3; j <= pitch_lag + 3; i++, j++) {
        /* Compute residual energy */
        energy[i << 1] = ff_dot_product(buf - j, buf - j, SUBFRAME_LEN);
        /* Compute correlation */
        energy[(i << 1) + 1] = ff_dot_product(buf, buf - j, SUBFRAME_LEN);
    }

    /* Compute target energy */
    energy[14] = ff_dot_product(buf, buf, SUBFRAME_LEN);

    /* Normalize */
    max = 0;
    for (i = 0; i < 15; i++)
        max = FFMAX(max, FFABS(energy[i]));

    exp = normalize_bits_int32(max);
    for (i = 0; i < 15; i++) {
        energy[i] = av_clipl_int32((int64_t)(energy[i] << exp) +
                                   (1 << 15)) >> 16;
    }

    hf->index = -1;
    hf->gain  =  0;
    max_ccr   =  1;
    max_eng   =  0x7fff;

    for (i = 0; i <= 6; i++) {
        eng = energy[i << 1];
        ccr = energy[(i << 1) + 1];

        if (ccr <= 0)
            continue;

        ccr  = (ccr * ccr + (1 << 14)) >> 15;
        diff = ccr * max_eng - eng * max_ccr;
        if (diff > 0) {
            max_ccr   = ccr;
            max_eng   = eng;
            hf->index = i;
        }
    }

    if (hf->index == -1) {
        hf->index = pitch_lag;
        return;
    }

    eng = energy[14] * max_eng;
    eng = (eng >> 2) + (eng >> 3);
    ccr = energy[(hf->index << 1) + 1] * energy[(hf->index << 1) + 1];
    if (eng < ccr) {
        eng = energy[(hf->index << 1) + 1];

        if (eng >= max_eng)
            hf->gain = 0x2800;
        else
            hf->gain = ((eng << 15) / max_eng * 0x2800 + (1 << 14)) >> 15;
    }
    hf->index += pitch_lag - 3;
}

/**
 * Apply the harmonic noise shaping filter.
 *
 * @param hf filter parameters
 */
static void harmonic_filter(HFParam *hf, const int16_t *src, int16_t *dest)
{
    int i;

    for (i = 0; i < SUBFRAME_LEN; i++) {
        int64_t temp = hf->gain * src[i - hf->index] << 1;
        dest[i] = av_clipl_int32((src[i] << 16) - temp + (1 << 15)) >> 16;
    }
}

static void harmonic_noise_sub(HFParam *hf, const int16_t *src, int16_t *dest)
{
    int i;
    for (i = 0; i < SUBFRAME_LEN; i++) {
        int64_t temp = hf->gain * src[i - hf->index] << 1;
        dest[i] = av_clipl_int32(((dest[i] - src[i]) << 16) + temp +
                                 (1 << 15)) >> 16;

    }
}

/**
 * Combined synthesis and formant perceptual weighting filer.
 *
 * @param qnt_lpc  quantized lpc coefficients
 * @param perf_lpc perceptual filter coefficients
 * @param perf_fir perceptual filter fir memory
 * @param perf_iir perceptual filter iir memory
 * @param scale    the filter output will be scaled by 2^scale
 */
static void synth_percept_filter(int16_t *qnt_lpc, int16_t *perf_lpc,
                                 int16_t *perf_fir, int16_t *perf_iir,
                                 const int16_t *src, int16_t *dest, int scale)
{
    int i, j;
    int16_t buf_16[SUBFRAME_LEN + LPC_ORDER];
    int64_t buf[SUBFRAME_LEN];

    int16_t *bptr_16 = buf_16 + LPC_ORDER;

    memcpy(buf_16, perf_fir, sizeof(int16_t) * LPC_ORDER);
    memcpy(dest - LPC_ORDER, perf_iir, sizeof(int16_t) * LPC_ORDER);

    for (i = 0; i < SUBFRAME_LEN; i++) {
        int64_t temp = 0;
        for (j = 1; j <= LPC_ORDER; j++)
            temp -= qnt_lpc[j - 1] * bptr_16[i - j];

        buf[i]     = (src[i] << 15) + (temp << 3);
        bptr_16[i] = av_clipl_int32(buf[i] + (1 << 15)) >> 16;
    }

    for (i = 0; i < SUBFRAME_LEN; i++) {
        int64_t fir = 0, iir = 0;
        for (j = 1; j <= LPC_ORDER; j++) {
            fir -= perf_lpc[j - 1] * bptr_16[i - j];
            iir += perf_lpc[j + LPC_ORDER - 1] * dest[i - j];
        }
        dest[i] = av_clipl_int32(((buf[i] + (fir << 3)) << scale) + (iir << 3) +
                                 (1 << 15)) >> 16;
    }
    memcpy(perf_fir, buf_16 + SUBFRAME_LEN, sizeof(int16_t) * LPC_ORDER);
    memcpy(perf_iir, dest + SUBFRAME_LEN - LPC_ORDER,
           sizeof(int16_t) * LPC_ORDER);
}

/**
 * Compute the adaptive codebook contribution.
 *
 * @param buf   input signal
 * @param index the current subframe index
 */
static void acb_search(G723_1_Context *p, int16_t *residual,
                       int16_t *impulse_resp, const int16_t *buf,
                       int index)
{

    int16_t flt_buf[PITCH_ORDER][SUBFRAME_LEN];

    const int16_t *cb_tbl = adaptive_cb_gain85;

    int ccr_buf[PITCH_ORDER * SUBFRAMES << 2];

    int pitch_lag = p->pitch_lag[index >> 1];
    int acb_lag   = 1;
    int acb_gain  = 0;
    int odd_frame = index & 1;
    int iter      = 3 + odd_frame;
    int count     = 0;
    int tbl_size  = 85;

    int i, j, k, l, max;
    int64_t temp;

    if (!odd_frame) {
        if (pitch_lag == PITCH_MIN)
            pitch_lag++;
        else
            pitch_lag = FFMIN(pitch_lag, PITCH_MAX - 5);
    }

    for (i = 0; i < iter; i++) {
        get_residual(residual, p->prev_excitation, pitch_lag + i - 1);

        for (j = 0; j < SUBFRAME_LEN; j++) {
            temp = 0;
            for (k = 0; k <= j; k++)
                temp += residual[PITCH_ORDER - 1 + k] * impulse_resp[j - k];
            flt_buf[PITCH_ORDER - 1][j] = av_clipl_int32((temp << 1) +
                                                         (1 << 15)) >> 16;
        }

        for (j = PITCH_ORDER - 2; j >= 0; j--) {
            flt_buf[j][0] = ((residual[j] << 13) + (1 << 14)) >> 15;
            for (k = 1; k < SUBFRAME_LEN; k++) {
                temp = (flt_buf[j + 1][k - 1] << 15) +
                       residual[j] * impulse_resp[k];
                flt_buf[j][k] = av_clipl_int32((temp << 1) + (1 << 15)) >> 16;
            }
        }

        /* Compute crosscorrelation with the signal */
        for (j = 0; j < PITCH_ORDER; j++) {
            temp = ff_dot_product(buf, flt_buf[j], SUBFRAME_LEN);
            ccr_buf[count++] = av_clipl_int32(temp << 1);
        }

        /* Compute energies */
        for (j = 0; j < PITCH_ORDER; j++) {
            ccr_buf[count++] = dot_product(flt_buf[j], flt_buf[j],
                                           SUBFRAME_LEN);
        }

        for (j = 1; j < PITCH_ORDER; j++) {
            for (k = 0; k < j; k++) {
                temp = ff_dot_product(flt_buf[j], flt_buf[k], SUBFRAME_LEN);
                ccr_buf[count++] = av_clipl_int32(temp<<2);
            }
        }
    }

    /* Normalize and shorten */
    max = 0;
    for (i = 0; i < 20 * iter; i++)
        max = FFMAX(max, FFABS(ccr_buf[i]));

    temp = normalize_bits_int32(max);

    for (i = 0; i < 20 * iter; i++){
        ccr_buf[i] = av_clipl_int32((int64_t)(ccr_buf[i] << temp) +
                                    (1 << 15)) >> 16;
    }

    max = 0;
    for (i = 0; i < iter; i++) {
        /* Select quantization table */
        if (!odd_frame && pitch_lag + i - 1 >= SUBFRAME_LEN - 2 ||
            odd_frame && pitch_lag >= SUBFRAME_LEN - 2) {
            cb_tbl = adaptive_cb_gain170;
            tbl_size = 170;
        }

        for (j = 0, k = 0; j < tbl_size; j++, k += 20) {
            temp = 0;
            for (l = 0; l < 20; l++)
                temp += ccr_buf[20 * i + l] * cb_tbl[k + l];
            temp =  av_clipl_int32(temp);

            if (temp > max) {
                max      = temp;
                acb_gain = j;
                acb_lag  = i;
            }
        }
    }

    if (!odd_frame) {
        pitch_lag += acb_lag - 1;
        acb_lag   =  1;
    }

    p->pitch_lag[index >> 1]      = pitch_lag;
    p->subframe[index].ad_cb_lag  = acb_lag;
    p->subframe[index].ad_cb_gain = acb_gain;
}

/**
 * Subtract the adaptive codebook contribution from the input
 * to obtain the residual.
 *
 * @param buf target vector
 */
static void sub_acb_contrib(const int16_t *residual, const int16_t *impulse_resp,
                            int16_t *buf)
{
    int i, j;
    /* Subtract adaptive CB contribution to obtain the residual */
    for (i = 0; i < SUBFRAME_LEN; i++) {
        int64_t temp = buf[i] << 14;
        for (j = 0; j <= i; j++)
            temp -= residual[j] * impulse_resp[i - j];

        buf[i] = av_clipl_int32((temp << 2) + (1 << 15)) >> 16;
    }
}

/**
 * Quantize the residual signal using the fixed codebook (MP-MLQ).
 *
 * @param optim optimized fixed codebook parameters
 * @param buf   excitation vector
 */
static void get_fcb_param(FCBParam *optim, int16_t *impulse_resp,
                          int16_t *buf, int pulse_cnt, int pitch_lag)
{
    FCBParam param;
    int16_t impulse_r[SUBFRAME_LEN];
    int16_t temp_corr[SUBFRAME_LEN];
    int16_t impulse_corr[SUBFRAME_LEN];

    int ccr1[SUBFRAME_LEN];
    int ccr2[SUBFRAME_LEN];
    int amp, err, max, max_amp_index, min, scale, i, j, k, l;

    int64_t temp;

    /* Update impulse response */
    memcpy(impulse_r, impulse_resp, sizeof(int16_t) * SUBFRAME_LEN);
    param.dirac_train = 0;
    if (pitch_lag < SUBFRAME_LEN - 2) {
        param.dirac_train = 1;
        gen_dirac_train(impulse_r, pitch_lag);
    }

    for (i = 0; i < SUBFRAME_LEN; i++)
        temp_corr[i] = impulse_r[i] >> 1;

    /* Compute impulse response autocorrelation */
    temp = dot_product(temp_corr, temp_corr, SUBFRAME_LEN);

    scale = normalize_bits_int32(temp);
    impulse_corr[0] = av_clipl_int32((temp << scale) + (1 << 15)) >> 16;

    for (i = 1; i < SUBFRAME_LEN; i++) {
        temp = dot_product(temp_corr + i, temp_corr, SUBFRAME_LEN - i);
        impulse_corr[i] = av_clipl_int32((temp << scale) + (1 << 15)) >> 16;
    }

    /* Compute crosscorrelation of impulse response with residual signal */
    scale -= 4;
    for (i = 0; i < SUBFRAME_LEN; i++){
        temp = dot_product(buf + i, impulse_r, SUBFRAME_LEN - i);
        if (scale < 0)
            ccr1[i] = temp >> -scale;
        else
            ccr1[i] = av_clipl_int32(temp << scale);
    }

    /* Search loop */
    for (i = 0; i < GRID_SIZE; i++) {
        /* Maximize the crosscorrelation */
        max = 0;
        for (j = i; j < SUBFRAME_LEN; j += GRID_SIZE) {
            temp = FFABS(ccr1[j]);
            if (temp >= max) {
                max = temp;
                param.pulse_pos[0] = j;
            }
        }

        /* Quantize the gain (max crosscorrelation/impulse_corr[0]) */
        amp = max;
        min = 1 << 30;
        max_amp_index = GAIN_LEVELS - 2;
        for (j = max_amp_index; j >= 2; j--) {
            temp = av_clipl_int32((int64_t)fixed_cb_gain[j] *
                                  impulse_corr[0] << 1);
            temp = FFABS(temp - amp);
            if (temp < min) {
                min = temp;
                max_amp_index = j;
            }
        }

        max_amp_index--;
        /* Select additional gain values */
        for (j = 1; j < 5; j++) {
            for (k = i; k < SUBFRAME_LEN; k += GRID_SIZE) {
                temp_corr[k] = 0;
                ccr2[k]      = ccr1[k];
            }
            param.amp_index = max_amp_index + j - 2;
            amp = fixed_cb_gain[param.amp_index];

            param.pulse_sign[0] = (ccr2[param.pulse_pos[0]] < 0) ? -amp : amp;
            temp_corr[param.pulse_pos[0]] = 1;

            for (k = 1; k < pulse_cnt; k++) {
                max = -1 << 30;
                for (l = i; l < SUBFRAME_LEN; l += GRID_SIZE) {
                    if (temp_corr[l])
                        continue;
                    temp = impulse_corr[FFABS(l - param.pulse_pos[k - 1])];
                    temp = av_clipl_int32((int64_t)temp *
                                          param.pulse_sign[k - 1] << 1);
                    ccr2[l] -= temp;
                    temp = FFABS(ccr2[l]);
                    if (temp > max) {
                        max = temp;
                        param.pulse_pos[k] = l;
                    }
                }

                param.pulse_sign[k] = (ccr2[param.pulse_pos[k]] < 0) ?
                                      -amp : amp;
                temp_corr[param.pulse_pos[k]] = 1;
            }

            /* Create the error vector */
            memset(temp_corr, 0, sizeof(int16_t) * SUBFRAME_LEN);

            for (k = 0; k < pulse_cnt; k++)
                temp_corr[param.pulse_pos[k]] = param.pulse_sign[k];

            for (k = SUBFRAME_LEN - 1; k >= 0; k--) {
                temp = 0;
                for (l = 0; l <= k; l++) {
                    int prod = av_clipl_int32((int64_t)temp_corr[l] *
                                              impulse_r[k - l] << 1);
                    temp     = av_clipl_int32(temp + prod);
                }
                temp_corr[k] = temp << 2 >> 16;
            }

            /* Compute square of error */
            err = 0;
            for (k = 0; k < SUBFRAME_LEN; k++) {
                int64_t prod;
                prod = av_clipl_int32((int64_t)buf[k] * temp_corr[k] << 1);
                err  = av_clipl_int32(err - prod);
                prod = av_clipl_int32((int64_t)temp_corr[k] * temp_corr[k]);
                err  = av_clipl_int32(err + prod);
            }

            /* Minimize */
            if (err < optim->min_err) {
                optim->min_err     = err;
                optim->grid_index  = i;
                optim->amp_index   = param.amp_index;
                optim->dirac_train = param.dirac_train;

                for (k = 0; k < pulse_cnt; k++) {
                    optim->pulse_sign[k] = param.pulse_sign[k];
                    optim->pulse_pos[k]  = param.pulse_pos[k];
                }
            }
        }
    }
}

/**
 * Encode the pulse position and gain of the current subframe.
 *
 * @param optim optimized fixed CB parameters
 * @param buf   excitation vector
 */
static void pack_fcb_param(G723_1_Subframe *subfrm, FCBParam *optim,
                           int16_t *buf, int pulse_cnt)
{
    int i, j;

    j = PULSE_MAX - pulse_cnt;

    subfrm->pulse_sign = 0;
    subfrm->pulse_pos  = 0;

    for (i = 0; i < SUBFRAME_LEN >> 1; i++) {
        int val = buf[optim->grid_index + (i << 1)];
        if (!val) {
            subfrm->pulse_pos += combinatorial_table[j][i];
        } else {
            subfrm->pulse_sign <<= 1;
            if (val < 0) subfrm->pulse_sign++;
            j++;

            if (j == PULSE_MAX) break;
        }
    }
    subfrm->amp_index   = optim->amp_index;
    subfrm->grid_index  = optim->grid_index;
    subfrm->dirac_train = optim->dirac_train;
}

/**
 * Compute the fixed codebook excitation.
 *
 * @param buf          target vector
 * @param impulse_resp impulse response of the combined filter
 */
static void fcb_search(G723_1_Context *p, int16_t *impulse_resp,
                       int16_t *buf, int index)
{
    FCBParam optim;
    int pulse_cnt = pulses[index];
    int i;

    optim.min_err = 1 << 30;
    get_fcb_param(&optim, impulse_resp, buf, pulse_cnt, SUBFRAME_LEN);

    if (p->pitch_lag[index >> 1] < SUBFRAME_LEN - 2) {
        get_fcb_param(&optim, impulse_resp, buf, pulse_cnt,
                      p->pitch_lag[index >> 1]);
    }

    /* Reconstruct the excitation */
    memset(buf, 0, sizeof(int16_t) * SUBFRAME_LEN);
    for (i = 0; i < pulse_cnt; i++)
        buf[optim.pulse_pos[i]] = optim.pulse_sign[i];

    pack_fcb_param(&p->subframe[index], &optim, buf, pulse_cnt);

    if (optim.dirac_train)
        gen_dirac_train(buf, p->pitch_lag[index >> 1]);
}

/**
 * Pack the frame parameters into output bitstream.
 *
 * @param frame output buffer
 * @param size  size of the buffer
 */
static int pack_bitstream(G723_1_Context *p, unsigned char *frame, int size)
{
    PutBitContext pb;
    int info_bits, i, temp;

    init_put_bits(&pb, frame, size);

    if (p->cur_rate == RATE_6300) {
        info_bits = 0;
        put_bits(&pb, 2, info_bits);
    }

    put_bits(&pb, 8, p->lsp_index[2]);
    put_bits(&pb, 8, p->lsp_index[1]);
    put_bits(&pb, 8, p->lsp_index[0]);

    put_bits(&pb, 7, p->pitch_lag[0] - PITCH_MIN);
    put_bits(&pb, 2, p->subframe[1].ad_cb_lag);
    put_bits(&pb, 7, p->pitch_lag[1] - PITCH_MIN);
    put_bits(&pb, 2, p->subframe[3].ad_cb_lag);

    /* Write 12 bit combined gain */
    for (i = 0; i < SUBFRAMES; i++) {
        temp = p->subframe[i].ad_cb_gain * GAIN_LEVELS +
               p->subframe[i].amp_index;
        if (p->cur_rate ==  RATE_6300)
            temp += p->subframe[i].dirac_train << 11;
        put_bits(&pb, 12, temp);
    }

    put_bits(&pb, 1, p->subframe[0].grid_index);
    put_bits(&pb, 1, p->subframe[1].grid_index);
    put_bits(&pb, 1, p->subframe[2].grid_index);
    put_bits(&pb, 1, p->subframe[3].grid_index);

    if (p->cur_rate == RATE_6300) {
        skip_put_bits(&pb, 1); /* reserved bit */

        /* Write 13 bit combined position index */
        temp = (p->subframe[0].pulse_pos >> 16) * 810 +
               (p->subframe[1].pulse_pos >> 14) *  90 +
               (p->subframe[2].pulse_pos >> 16) *   9 +
               (p->subframe[3].pulse_pos >> 14);
        put_bits(&pb, 13, temp);

        put_bits(&pb, 16, p->subframe[0].pulse_pos & 0xffff);
        put_bits(&pb, 14, p->subframe[1].pulse_pos & 0x3fff);
        put_bits(&pb, 16, p->subframe[2].pulse_pos & 0xffff);
        put_bits(&pb, 14, p->subframe[3].pulse_pos & 0x3fff);

        put_bits(&pb, 6, p->subframe[0].pulse_sign);
        put_bits(&pb, 5, p->subframe[1].pulse_sign);
        put_bits(&pb, 6, p->subframe[2].pulse_sign);
        put_bits(&pb, 5, p->subframe[3].pulse_sign);
    }

    flush_put_bits(&pb);
    return frame_size[info_bits];
}

static int g723_1_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    G723_1_Context *p = avctx->priv_data;
    int16_t unq_lpc[LPC_ORDER * SUBFRAMES];
    int16_t qnt_lpc[LPC_ORDER * SUBFRAMES];
    int16_t cur_lsp[LPC_ORDER];
    int16_t weighted_lpc[LPC_ORDER * SUBFRAMES << 1];
    int16_t vector[FRAME_LEN + PITCH_MAX];
    int offset, ret;
    int16_t *in = (const int16_t *)frame->data[0];

    HFParam hf[4];
    int i, j;

    highpass_filter(in, &p->hpf_fir_mem, &p->hpf_iir_mem);

    memcpy(vector, p->prev_data, HALF_FRAME_LEN * sizeof(int16_t));
    memcpy(vector + HALF_FRAME_LEN, in, FRAME_LEN * sizeof(int16_t));

    comp_lpc_coeff(vector, unq_lpc);
    lpc2lsp(&unq_lpc[LPC_ORDER * 3], p->prev_lsp, cur_lsp);
    lsp_quantize(p->lsp_index, cur_lsp, p->prev_lsp);

    /* Update memory */
    memcpy(vector + LPC_ORDER, p->prev_data + SUBFRAME_LEN,
           sizeof(int16_t) * SUBFRAME_LEN);
    memcpy(vector + LPC_ORDER + SUBFRAME_LEN, in,
           sizeof(int16_t) * (HALF_FRAME_LEN + SUBFRAME_LEN));
    memcpy(p->prev_data, in + HALF_FRAME_LEN,
           sizeof(int16_t) * HALF_FRAME_LEN);
    memcpy(in, vector + LPC_ORDER, sizeof(int16_t) * FRAME_LEN);

    perceptual_filter(p, weighted_lpc, unq_lpc, vector);

    memcpy(in, vector + LPC_ORDER, sizeof(int16_t) * FRAME_LEN);
    memcpy(vector, p->prev_weight_sig, sizeof(int16_t) * PITCH_MAX);
    memcpy(vector + PITCH_MAX, in, sizeof(int16_t) * FRAME_LEN);

    scale_vector(vector, vector, FRAME_LEN + PITCH_MAX);

    p->pitch_lag[0] = estimate_pitch(vector, PITCH_MAX);
    p->pitch_lag[1] = estimate_pitch(vector, PITCH_MAX + HALF_FRAME_LEN);

    for (i = PITCH_MAX, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
        comp_harmonic_coeff(vector + i, p->pitch_lag[j >> 1], hf + j);

    memcpy(vector, p->prev_weight_sig, sizeof(int16_t) * PITCH_MAX);
    memcpy(vector + PITCH_MAX, in, sizeof(int16_t) * FRAME_LEN);
    memcpy(p->prev_weight_sig, vector + FRAME_LEN, sizeof(int16_t) * PITCH_MAX);

    for (i = 0, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
        harmonic_filter(hf + j, vector + PITCH_MAX + i, in + i);

    inverse_quant(cur_lsp, p->prev_lsp, p->lsp_index, 0);
    lsp_interpolate(qnt_lpc, cur_lsp, p->prev_lsp);

    memcpy(p->prev_lsp, cur_lsp, sizeof(int16_t) * LPC_ORDER);

    offset = 0;
    for (i = 0; i < SUBFRAMES; i++) {
        int16_t impulse_resp[SUBFRAME_LEN];
        int16_t residual[SUBFRAME_LEN + PITCH_ORDER - 1];
        int16_t flt_in[SUBFRAME_LEN];
        int16_t zero[LPC_ORDER], fir[LPC_ORDER], iir[LPC_ORDER];

        /**
         * Compute the combined impulse response of the synthesis filter,
         * formant perceptual weighting filter and harmonic noise shaping filter
         */
        memset(zero, 0, sizeof(int16_t) * LPC_ORDER);
        memset(vector, 0, sizeof(int16_t) * PITCH_MAX);
        memset(flt_in, 0, sizeof(int16_t) * SUBFRAME_LEN);

        flt_in[0] = 1 << 13; /* Unit impulse */
        synth_percept_filter(qnt_lpc + offset, weighted_lpc + (offset << 1),
                             zero, zero, flt_in, vector + PITCH_MAX, 1);
        harmonic_filter(hf + i, vector + PITCH_MAX, impulse_resp);

         /* Compute the combined zero input response */
        flt_in[0] = 0;
        memcpy(fir, p->perf_fir_mem, sizeof(int16_t) * LPC_ORDER);
        memcpy(iir, p->perf_iir_mem, sizeof(int16_t) * LPC_ORDER);

        synth_percept_filter(qnt_lpc + offset, weighted_lpc + (offset << 1),
                             fir, iir, flt_in, vector + PITCH_MAX, 0);
        memcpy(vector, p->harmonic_mem, sizeof(int16_t) * PITCH_MAX);
        harmonic_noise_sub(hf + i, vector + PITCH_MAX, in);

        acb_search(p, residual, impulse_resp, in, i);
        gen_acb_excitation(residual, p->prev_excitation,p->pitch_lag[i >> 1],
                           &p->subframe[i], p->cur_rate);
        sub_acb_contrib(residual, impulse_resp, in);

        fcb_search(p, impulse_resp, in, i);

        /* Reconstruct the excitation */
        gen_acb_excitation(impulse_resp, p->prev_excitation, p->pitch_lag[i >> 1],
                           &p->subframe[i], RATE_6300);

        memmove(p->prev_excitation, p->prev_excitation + SUBFRAME_LEN,
               sizeof(int16_t) * (PITCH_MAX - SUBFRAME_LEN));
        for (j = 0; j < SUBFRAME_LEN; j++)
            in[j] = av_clip_int16((in[j] << 1) + impulse_resp[j]);
        memcpy(p->prev_excitation + PITCH_MAX - SUBFRAME_LEN, in,
               sizeof(int16_t) * SUBFRAME_LEN);

        /* Update filter memories */
        synth_percept_filter(qnt_lpc + offset, weighted_lpc + (offset << 1),
                             p->perf_fir_mem, p->perf_iir_mem,
                             in, vector + PITCH_MAX, 0);
        memmove(p->harmonic_mem, p->harmonic_mem + SUBFRAME_LEN,
                sizeof(int16_t) * (PITCH_MAX - SUBFRAME_LEN));
        memcpy(p->harmonic_mem + PITCH_MAX - SUBFRAME_LEN, vector + PITCH_MAX,
               sizeof(int16_t) * SUBFRAME_LEN);

        in += SUBFRAME_LEN;
        offset += LPC_ORDER;
    }

    if ((ret = ff_alloc_packet2(avctx, avpkt, 24)) < 0)
        return ret;

    *got_packet_ptr = 1;
    avpkt->size = pack_bitstream(p, avpkt->data, avpkt->size);
    return 0;
}

AVCodec ff_g723_1_encoder = {
    .name           = "g723_1",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_G723_1,
    .priv_data_size = sizeof(G723_1_Context),
    .init           = g723_1_encode_init,
    .encode2        = g723_1_encode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("G.723.1"),
    .sample_fmts    = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,
                                                    AV_SAMPLE_FMT_NONE},
};
#endif
