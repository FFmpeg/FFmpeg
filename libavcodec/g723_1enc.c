/*
 * G.723.1 compatible encoder
 * Copyright (c) Mohamed Naufal <naufal22@gmail.com>
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
 * G.723.1 compatible encoder
 */

#include <stdint.h>
#include <string.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "celp_math.h"
#include "encode.h"
#include "g723_1.h"
#include "internal.h"

#define BITSTREAM_WRITER_LE
#include "put_bits.h"

/**
 * Hamming window coefficients scaled by 2^15
 */
static const int16_t hamming_window[LPC_FRAME] = {
     2621,  2631,  2659,  2705,  2770,  2853,  2955,  3074,  3212,  3367,
     3541,  3731,  3939,  4164,  4405,  4663,  4937,  5226,  5531,  5851,
     6186,  6534,  6897,  7273,  7661,  8062,  8475,  8899,  9334,  9780,
    10235, 10699, 11172, 11653, 12141, 12636, 13138, 13645, 14157, 14673,
    15193, 15716, 16242, 16769, 17298, 17827, 18356, 18884, 19411, 19935,
    20457, 20975, 21489, 21999, 22503, 23002, 23494, 23978, 24455, 24924,
    25384, 25834, 26274, 26704, 27122, 27529, 27924, 28306, 28675, 29031,
    29373, 29700, 30012, 30310, 30592, 30857, 31107, 31340, 31557, 31756,
    31938, 32102, 32249, 32377, 32488, 32580, 32654, 32710, 32747, 32766,
    32766, 32747, 32710, 32654, 32580, 32488, 32377, 32249, 32102, 31938,
    31756, 31557, 31340, 31107, 30857, 30592, 30310, 30012, 29700, 29373,
    29031, 28675, 28306, 27924, 27529, 27122, 26704, 26274, 25834, 25384,
    24924, 24455, 23978, 23494, 23002, 22503, 21999, 21489, 20975, 20457,
    19935, 19411, 18884, 18356, 17827, 17298, 16769, 16242, 15716, 15193,
    14673, 14157, 13645, 13138, 12636, 12141, 11653, 11172, 10699, 10235,
     9780, 9334,   8899,  8475,  8062,  7661,  7273,  6897,  6534,  6186,
     5851, 5531,   5226,  4937,  4663,  4405,  4164,  3939,  3731,  3541,
     3367, 3212,   3074,  2955,  2853,  2770,  2705,  2659,  2631,  2621
};

/**
 * Binomial window coefficients scaled by 2^15
 */
static const int16_t binomial_window[LPC_ORDER] = {
    32749, 32695, 32604, 32477, 32315, 32118, 31887, 31622, 31324, 30995
};

/**
 * 0.994^i scaled by 2^15
 */
static const int16_t bandwidth_expand[LPC_ORDER] = {
    32571, 32376, 32182, 31989, 31797, 31606, 31416, 31228, 31040, 30854
};

/**
 * 0.5^i scaled by 2^15
 */
static const int16_t percept_flt_tbl[2][LPC_ORDER] = {
    /* Zero part */
    {29491, 26542, 23888, 21499, 19349, 17414, 15673, 14106, 12695, 11425},
    /* Pole part */
    {16384,  8192,  4096,  2048,  1024,   512,   256,   128,    64,    32}
};

static av_cold int g723_1_encode_init(AVCodecContext *avctx)
{
    G723_1_Context *s = avctx->priv_data;
    G723_1_ChannelContext *p = &s->ch[0];

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->bit_rate == 6300) {
        p->cur_rate = RATE_6300;
    } else if (avctx->bit_rate == 5300) {
        av_log(avctx, AV_LOG_ERROR, "Use bitrate 6300 instead of 5300.\n");
        avpriv_report_missing_feature(avctx, "Bitrate 5300");
        return AVERROR_PATCHWELCOME;
    } else {
        av_log(avctx, AV_LOG_ERROR, "Bitrate not supported, use 6300\n");
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

    ff_g723_1_scale_vector(vector, buf, LPC_FRAME);

    /* Apply the Hamming window */
    for (i = 0; i < LPC_FRAME; i++)
        vector[i] = (vector[i] * hamming_window[i] + (1 << 14)) >> 15;

    /* Compute the first autocorrelation coefficient */
    temp = ff_dot_product(vector, vector, LPC_FRAME);

    /* Apply a white noise correlation factor of (1025/1024) */
    temp += temp >> 10;

    /* Normalize */
    scale       = ff_g723_1_normalize_bits(temp, 31);
    autocorr[0] = av_clipl_int32((int64_t) (temp << scale) +
                                 (1 << 15)) >> 16;

    /* Compute the remaining coefficients */
    if (!autocorr[0]) {
        memset(autocorr + 1, 0, LPC_ORDER * sizeof(int16_t));
    } else {
        for (i = 1; i <= LPC_ORDER; i++) {
            temp        = ff_dot_product(vector, vector + i, LPC_FRAME - i);
            temp        = MULL2((temp << scale), binomial_window[i - 1]);
            autocorr[i] = av_clipl_int32((int64_t) temp + (1 << 15)) >> 16;
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

        lpc[i] = av_clipl_int32((int64_t) (partial_corr << 14) +
                                (1 << 15)) >> 16;

        /* Update the prediction error */
        temp  = MULL2(temp, partial_corr);
        error = av_clipl_int32((int64_t) (error << 16) - temp +
                               (1 << 15)) >> 16;

        memcpy(vector, lpc, i * sizeof(int16_t));
        for (j = 0; j < i; j++) {
            temp   = partial_corr * vector[i - j - 1] << 1;
            lpc[j] = av_clipl_int32((int64_t) (lpc[j] << 16) - temp +
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

        lpc_ptr      += LPC_ORDER;
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
    f[LPC_ORDER]     >>= 1;
    f[LPC_ORDER + 1] >>= 1;

    /* Normalize and shorten */
    max = FFABS(f[0]);
    for (i = 1; i < LPC_ORDER + 2; i++)
        max = FFMAX(max, FFABS(f[i]));

    shift = ff_g723_1_normalize_bits(max, 31);

    for (i = 0; i < LPC_ORDER + 2; i++)
        f[i] = av_clipl_int32((int64_t) (f[i] << shift) + (1 << 15)) >> 16;

    /**
     * Evaluate F1 and F2 at uniform intervals of pi/256 along the
     * unit circle and check for zero crossings.
     */
    p    = 0;
    temp = 0;
    for (i = 0; i <= LPC_ORDER / 2; i++)
        temp += f[2 * i] * G723_1_COS_TAB_FIRST_ELEMENT;
    prev_val = av_clipl_int32(temp << 1);
    count    = 0;
    for (i = 1; i < COS_TBL_SIZE / 2; i++) {
        /* Evaluate */
        temp = 0;
        for (j = 0; j <= LPC_ORDER / 2; j++)
            temp += f[LPC_ORDER - 2 * j + p] * ff_g723_1_cos_tab[i * j % COS_TBL_SIZE];
        cur_val = av_clipl_int32(temp << 1);

        /* Check for sign change, indicating a zero crossing */
        if ((cur_val ^ prev_val) < 0) {
            int abs_cur  = FFABS(cur_val);
            int abs_prev = FFABS(prev_val);
            int sum      = abs_cur + abs_prev;

            shift        = ff_g723_1_normalize_bits(sum, 31);
            sum        <<= shift;
            abs_prev     = abs_prev << shift >> 8;
            lsp[count++] = ((i - 1) << 7) + (abs_prev >> 1) / (sum >> 16);

            if (count == LPC_ORDER)
                break;

            /* Switch between sum and difference polynomials */
            p ^= 1;

            /* Evaluate */
            temp = 0;
            for (j = 0; j <= LPC_ORDER / 2; j++)
                temp += f[LPC_ORDER - 2 * j + p] *
                        ff_g723_1_cos_tab[i * j % COS_TBL_SIZE];
            cur_val = av_clipl_int32(temp << 1);
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
#define get_index(num, offset, size)                                          \
{                                                                             \
    int error, max = -1;                                                      \
    int16_t temp[4];                                                          \
    int i, j;                                                                 \
                                                                              \
    for (i = 0; i < LSP_CB_SIZE; i++) {                                       \
        for (j = 0; j < size; j++){                                           \
            temp[j] = (weight[j + (offset)] * ff_g723_1_lsp_band##num[i][j] + \
                      (1 << 14)) >> 15;                                       \
        }                                                                     \
        error  = ff_g723_1_dot_product(lsp + (offset), temp, size) << 1;      \
        error -= ff_g723_1_dot_product(ff_g723_1_lsp_band##num[i], temp, size); \
        if (error > max) {                                                    \
            max = error;                                                      \
            lsp_index[num] = i;                                               \
        }                                                                     \
    }                                                                         \
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
    weight[0]             = (1 << 20) / (lsp[1] - lsp[0]);
    weight[LPC_ORDER - 1] = (1 << 20) /
                            (lsp[LPC_ORDER - 1] - lsp[LPC_ORDER - 2]);

    for (i = 1; i < LPC_ORDER - 1; i++) {
        min = FFMIN(lsp[i] - lsp[i - 1], lsp[i + 1] - lsp[i]);
        if (min > 0x20)
            weight[i] = (1 << 20) / min;
        else
            weight[i] = INT16_MAX;
    }

    /* Normalize */
    max = 0;
    for (i = 0; i < LPC_ORDER; i++)
        max = FFMAX(weight[i], max);

    shift = ff_g723_1_normalize_bits(max, 15);
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
 * Perform IIR filtering.
 *
 * @param fir_coef FIR coefficients
 * @param iir_coef IIR coefficients
 * @param src      source vector
 * @param dest     destination vector
 */
static void iir_filter(int16_t *fir_coef, int16_t *iir_coef,
                       int16_t *src, int16_t *dest)
{
    int m, n;

    for (m = 0; m < SUBFRAME_LEN; m++) {
        int64_t filter = 0;
        for (n = 1; n <= LPC_ORDER; n++) {
            filter -= fir_coef[n - 1] * src[m - n] -
                      iir_coef[n - 1] * dest[m - n];
        }

        dest[m] = av_clipl_int32((src[m] << 16) + (filter << 3) +
                                 (1 << 15)) >> 16;
    }
}

/**
 * Apply the formant perceptual weighting filter.
 *
 * @param flt_coef filter coefficients
 * @param unq_lpc  unquantized lpc vector
 */
static void perceptual_filter(G723_1_ChannelContext *p, int16_t *flt_coef,
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
        iir_filter(flt_coef + 2 * l, flt_coef + 2 * l + LPC_ORDER,
                   vector + i, buf + i);
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
        ccr = ff_dot_product(buf + start, buf + offset, HALF_FRAME_LEN);
        if (ccr <= 0)
            continue;

        /* Split into mantissa and exponent to maintain precision */
        exp   = ff_g723_1_normalize_bits(ccr, 31);
        ccr   = av_clipl_int32((int64_t) (ccr << exp) + (1 << 15)) >> 16;
        exp <<= 1;
        ccr  *= ccr;
        temp  = ff_g723_1_normalize_bits(ccr, 31);
        ccr   = ccr << temp >> 16;
        exp  += temp;

        temp = ff_g723_1_normalize_bits(orig_eng, 31);
        eng  = av_clipl_int32((int64_t) (orig_eng << temp) + (1 << 15)) >> 16;
        exp -= temp;

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

    exp = ff_g723_1_normalize_bits(max, 31);
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
static void acb_search(G723_1_ChannelContext *p, int16_t *residual,
                       int16_t *impulse_resp, const int16_t *buf,
                       int index)
{
    int16_t flt_buf[PITCH_ORDER][SUBFRAME_LEN];

    const int16_t *cb_tbl = ff_g723_1_adaptive_cb_gain85;

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
        ff_g723_1_get_residual(residual, p->prev_excitation, pitch_lag + i - 1);

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
            temp             = ff_dot_product(buf, flt_buf[j], SUBFRAME_LEN);
            ccr_buf[count++] = av_clipl_int32(temp << 1);
        }

        /* Compute energies */
        for (j = 0; j < PITCH_ORDER; j++) {
            ccr_buf[count++] = ff_g723_1_dot_product(flt_buf[j], flt_buf[j],
                                                     SUBFRAME_LEN);
        }

        for (j = 1; j < PITCH_ORDER; j++) {
            for (k = 0; k < j; k++) {
                temp             = ff_dot_product(flt_buf[j], flt_buf[k], SUBFRAME_LEN);
                ccr_buf[count++] = av_clipl_int32(temp << 2);
            }
        }
    }

    /* Normalize and shorten */
    max = 0;
    for (i = 0; i < 20 * iter; i++)
        max = FFMAX(max, FFABS(ccr_buf[i]));

    temp = ff_g723_1_normalize_bits(max, 31);

    for (i = 0; i < 20 * iter; i++)
        ccr_buf[i] = av_clipl_int32((int64_t) (ccr_buf[i] << temp) +
                                    (1 << 15)) >> 16;

    max = 0;
    for (i = 0; i < iter; i++) {
        /* Select quantization table */
        if (!odd_frame && pitch_lag + i - 1 >= SUBFRAME_LEN - 2 ||
            odd_frame && pitch_lag >= SUBFRAME_LEN - 2) {
            cb_tbl   = ff_g723_1_adaptive_cb_gain170;
            tbl_size = 170;
        }

        for (j = 0, k = 0; j < tbl_size; j++, k += 20) {
            temp = 0;
            for (l = 0; l < 20; l++)
                temp += ccr_buf[20 * i + l] * cb_tbl[k + l];
            temp = av_clipl_int32(temp);

            if (temp > max) {
                max      = temp;
                acb_gain = j;
                acb_lag  = i;
            }
        }
    }

    if (!odd_frame) {
        pitch_lag += acb_lag - 1;
        acb_lag    = 1;
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
        ff_g723_1_gen_dirac_train(impulse_r, pitch_lag);
    }

    for (i = 0; i < SUBFRAME_LEN; i++)
        temp_corr[i] = impulse_r[i] >> 1;

    /* Compute impulse response autocorrelation */
    temp = ff_g723_1_dot_product(temp_corr, temp_corr, SUBFRAME_LEN);

    scale           = ff_g723_1_normalize_bits(temp, 31);
    impulse_corr[0] = av_clipl_int32((temp << scale) + (1 << 15)) >> 16;

    for (i = 1; i < SUBFRAME_LEN; i++) {
        temp = ff_g723_1_dot_product(temp_corr + i, temp_corr,
                                     SUBFRAME_LEN - i);
        impulse_corr[i] = av_clipl_int32((temp << scale) + (1 << 15)) >> 16;
    }

    /* Compute crosscorrelation of impulse response with residual signal */
    scale -= 4;
    for (i = 0; i < SUBFRAME_LEN; i++) {
        temp = ff_g723_1_dot_product(buf + i, impulse_r, SUBFRAME_LEN - i);
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
                max                = temp;
                param.pulse_pos[0] = j;
            }
        }

        /* Quantize the gain (max crosscorrelation/impulse_corr[0]) */
        amp           = max;
        min           = 1 << 30;
        max_amp_index = GAIN_LEVELS - 2;
        for (j = max_amp_index; j >= 2; j--) {
            temp = av_clipl_int32((int64_t) ff_g723_1_fixed_cb_gain[j] *
                                  impulse_corr[0] << 1);
            temp = FFABS(temp - amp);
            if (temp < min) {
                min           = temp;
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
            amp             = ff_g723_1_fixed_cb_gain[param.amp_index];

            param.pulse_sign[0] = (ccr2[param.pulse_pos[0]] < 0) ? -amp : amp;
            temp_corr[param.pulse_pos[0]] = 1;

            for (k = 1; k < pulse_cnt; k++) {
                max = INT_MIN;
                for (l = i; l < SUBFRAME_LEN; l += GRID_SIZE) {
                    if (temp_corr[l])
                        continue;
                    temp = impulse_corr[FFABS(l - param.pulse_pos[k - 1])];
                    temp = av_clipl_int32((int64_t) temp *
                                          param.pulse_sign[k - 1] << 1);
                    ccr2[l] -= temp;
                    temp     = FFABS(ccr2[l]);
                    if (temp > max) {
                        max                = temp;
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
                    int prod = av_clipl_int32((int64_t) temp_corr[l] *
                                              impulse_r[k - l] << 1);
                    temp = av_clipl_int32(temp + prod);
                }
                temp_corr[k] = temp << 2 >> 16;
            }

            /* Compute square of error */
            err = 0;
            for (k = 0; k < SUBFRAME_LEN; k++) {
                int64_t prod;
                prod = av_clipl_int32((int64_t) buf[k] * temp_corr[k] << 1);
                err  = av_clipl_int32(err - prod);
                prod = av_clipl_int32((int64_t) temp_corr[k] * temp_corr[k]);
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
            subfrm->pulse_pos += ff_g723_1_combinatorial_table[j][i];
        } else {
            subfrm->pulse_sign <<= 1;
            if (val < 0)
                subfrm->pulse_sign++;
            j++;

            if (j == PULSE_MAX)
                break;
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
static void fcb_search(G723_1_ChannelContext *p, int16_t *impulse_resp,
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
        ff_g723_1_gen_dirac_train(buf, p->pitch_lag[index >> 1]);
}

/**
 * Pack the frame parameters into output bitstream.
 *
 * @param frame output buffer
 * @param size  size of the buffer
 */
static void pack_bitstream(G723_1_ChannelContext *p, AVPacket *avpkt, int info_bits)
{
    PutBitContext pb;
    int i, temp;

    init_put_bits(&pb, avpkt->data, avpkt->size);

    put_bits(&pb, 2, info_bits);

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
        if (p->cur_rate == RATE_6300)
            temp += p->subframe[i].dirac_train << 11;
        put_bits(&pb, 12, temp);
    }

    put_bits(&pb, 1, p->subframe[0].grid_index);
    put_bits(&pb, 1, p->subframe[1].grid_index);
    put_bits(&pb, 1, p->subframe[2].grid_index);
    put_bits(&pb, 1, p->subframe[3].grid_index);

    if (p->cur_rate == RATE_6300) {
        put_bits(&pb, 1, 0); /* reserved bit */

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
}

static int g723_1_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                               const AVFrame *frame, int *got_packet_ptr)
{
    G723_1_Context *s = avctx->priv_data;
    G723_1_ChannelContext *p = &s->ch[0];
    int16_t unq_lpc[LPC_ORDER * SUBFRAMES];
    int16_t qnt_lpc[LPC_ORDER * SUBFRAMES];
    int16_t cur_lsp[LPC_ORDER];
    int16_t weighted_lpc[LPC_ORDER * SUBFRAMES << 1];
    int16_t vector[FRAME_LEN + PITCH_MAX];
    int offset, ret, i, j, info_bits = 0;
    int16_t *in, *start;
    HFParam hf[4];

    /* duplicate input */
    start = in = av_memdup(frame->data[0], frame->nb_samples * sizeof(int16_t));
    if (!in)
        return AVERROR(ENOMEM);

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

    ff_g723_1_scale_vector(vector, vector, FRAME_LEN + PITCH_MAX);

    p->pitch_lag[0] = estimate_pitch(vector, PITCH_MAX);
    p->pitch_lag[1] = estimate_pitch(vector, PITCH_MAX + HALF_FRAME_LEN);

    for (i = PITCH_MAX, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
        comp_harmonic_coeff(vector + i, p->pitch_lag[j >> 1], hf + j);

    memcpy(vector, p->prev_weight_sig, sizeof(int16_t) * PITCH_MAX);
    memcpy(vector + PITCH_MAX, in, sizeof(int16_t) * FRAME_LEN);
    memcpy(p->prev_weight_sig, vector + FRAME_LEN, sizeof(int16_t) * PITCH_MAX);

    for (i = 0, j = 0; j < SUBFRAMES; i += SUBFRAME_LEN, j++)
        harmonic_filter(hf + j, vector + PITCH_MAX + i, in + i);

    ff_g723_1_inverse_quant(cur_lsp, p->prev_lsp, p->lsp_index, 0);
    ff_g723_1_lsp_interpolate(qnt_lpc, cur_lsp, p->prev_lsp);

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
        ff_g723_1_gen_acb_excitation(residual, p->prev_excitation,
                                     p->pitch_lag[i >> 1], &p->subframe[i],
                                     p->cur_rate);
        sub_acb_contrib(residual, impulse_resp, in);

        fcb_search(p, impulse_resp, in, i);

        /* Reconstruct the excitation */
        ff_g723_1_gen_acb_excitation(impulse_resp, p->prev_excitation,
                                     p->pitch_lag[i >> 1], &p->subframe[i],
                                     RATE_6300);

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

        in     += SUBFRAME_LEN;
        offset += LPC_ORDER;
    }

    av_free(start);

    ret = ff_get_encode_buffer(avctx, avpkt, frame_size[info_bits], 0);
    if (ret < 0)
        return ret;

    *got_packet_ptr = 1;
    pack_bitstream(p, avpkt, info_bits);
    return 0;
}

static const AVCodecDefault defaults[] = {
    { "b", "6300" },
    { NULL },
};

const AVCodec ff_g723_1_encoder = {
    .name           = "g723_1",
    .long_name      = NULL_IF_CONFIG_SMALL("G.723.1"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_G723_1,
    .capabilities   = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(G723_1_Context),
    .init           = g723_1_encode_init,
    .encode2        = g723_1_encode_frame,
    .defaults       = defaults,
    .sample_fmts    = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE
    },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
