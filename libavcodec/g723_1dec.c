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

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#define BITSTREAM_READER_LE
#include "acelp_vectors.h"
#include "avcodec.h"
#include "celp_filters.h"
#include "celp_math.h"
#include "get_bits.h"
#include "internal.h"
#include "g723_1.h"

#define CNG_RANDOM_SEED 12345

static av_cold int g723_1_decode_init(AVCodecContext *avctx)
{
    G723_1_Context *s = avctx->priv_data;

    avctx->sample_fmt     = AV_SAMPLE_FMT_S16P;
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Only mono and stereo are supported (requested channels: %d).\n", avctx->channels);
        return AVERROR(EINVAL);
    }
    avctx->channel_layout = avctx->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    for (int ch = 0; ch < avctx->channels; ch++) {
        G723_1_ChannelContext *p = &s->ch[ch];

        p->pf_gain = 1 << 12;

        memcpy(p->prev_lsp, dc_lsp, LPC_ORDER * sizeof(*p->prev_lsp));
        memcpy(p->sid_lsp,  dc_lsp, LPC_ORDER * sizeof(*p->sid_lsp));

        p->cng_random_seed = CNG_RANDOM_SEED;
        p->past_frame_type = SID_FRAME;
    }

    return 0;
}

/**
 * Unpack the frame into parameters.
 *
 * @param p           the context
 * @param buf         pointer to the input buffer
 * @param buf_size    size of the input buffer
 */
static int unpack_bitstream(G723_1_ChannelContext *p, const uint8_t *buf,
                            int buf_size)
{
    GetBitContext gb;
    int ad_cb_len;
    int temp, info_bits, i;
    int ret;

    ret = init_get_bits8(&gb, buf, buf_size);
    if (ret < 0)
        return ret;

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
            ff_g723_1_gen_dirac_train(vector, pitch_lag);
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
        ccr = ff_g723_1_dot_product(buf, buf + dir * i, length);

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
static void comp_ppf_coeff(G723_1_ChannelContext *p, int offset, int pitch_lag,
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
    energy[0] = ff_g723_1_dot_product(buf, buf, SUBFRAME_LEN);

    /* Compute forward residual energy */
    if (fwd_lag)
        energy[2] = ff_g723_1_dot_product(buf + fwd_lag, buf + fwd_lag,
                                          SUBFRAME_LEN);

    /* Compute backward residual energy */
    if (back_lag)
        energy[4] = ff_g723_1_dot_product(buf - back_lag, buf - back_lag,
                                          SUBFRAME_LEN);

    /* Normalize and shorten */
    temp1 = 0;
    for (i = 0; i < 5; i++)
        temp1 = FFMAX(energy[i], temp1);

    scale = ff_g723_1_normalize_bits(temp1, 31);
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
static int comp_interp_index(G723_1_ChannelContext *p, int pitch_lag,
                             int *exc_eng, int *scale)
{
    int offset = PITCH_MAX + 2 * SUBFRAME_LEN;
    int16_t *buf = p->audio + LPC_ORDER;

    int index, ccr, tgt_eng, best_eng, temp;

    *scale = ff_g723_1_scale_vector(buf, p->excitation, FRAME_LEN + PITCH_MAX);
    buf   += offset;

    /* Compute maximum backward cross-correlation */
    ccr   = 0;
    index = autocorr_max(buf, offset, &ccr, pitch_lag, SUBFRAME_LEN * 2, -1);
    ccr   = av_sat_add32(ccr, 1 << 15) >> 16;

    /* Compute target energy */
    tgt_eng  = ff_g723_1_dot_product(buf, buf, SUBFRAME_LEN * 2);
    *exc_eng = av_sat_add32(tgt_eng, 1 << 15) >> 16;

    if (ccr <= 0)
        return 0;

    /* Compute best energy */
    best_eng = ff_g723_1_dot_product(buf - index, buf - index,
                                     SUBFRAME_LEN * 2);
    best_eng = av_sat_add32(best_eng, 1 << 15) >> 16;

    temp = best_eng * *exc_eng >> 3;

    if (temp < ccr * ccr) {
        return index;
    } else
        return 0;
}

/**
 * Perform residual interpolation based on frame classification.
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
            *rseed = (int16_t)(*rseed * 521 + 259);
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
        (dest)[m] = av_clipl_int32(((src)[m] * 65536) + (filter * 8) +\
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
static void gain_scale(G723_1_ChannelContext *p, int16_t * buf, int energy)
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
        bits1   = ff_g723_1_normalize_bits(num,   31);
        bits2   = ff_g723_1_normalize_bits(denom, 31);
        num     = num << bits1 >> 1;
        denom <<= bits2;

        bits2 = 5 + bits1 - bits2;
        bits2 = av_clip_uintp2(bits2, 5);

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
static void formant_postfilter(G723_1_ChannelContext *p, int16_t *lpc,
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
        iir_filter(filter_coef[0], filter_coef[1], buf + i, filter_signal + i, 1);
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
        scale = ff_g723_1_scale_vector(dst, buf, SUBFRAME_LEN);

        /* Compute auto correlation coefficients */
        auto_corr[0] = ff_g723_1_dot_product(dst, dst + 1, SUBFRAME_LEN - 1);
        auto_corr[1] = ff_g723_1_dot_product(dst, dst,     SUBFRAME_LEN);

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

static int estimate_sid_gain(G723_1_ChannelContext *p)
{
    int i, shift, seg, seg2, t, val, val_add, x, y;

    shift = 16 - p->cur_gain * 2;
    if (shift > 0) {
        if (p->sid_gain == 0) {
            t = 0;
        } else if (shift >= 31 || (int32_t)((uint32_t)p->sid_gain << shift) >> shift != p->sid_gain) {
            if (p->sid_gain < 0) t = INT32_MIN;
            else                 t = INT32_MAX;
        } else
            t = p->sid_gain << shift;
    }else
        t = p->sid_gain >> -shift;
    x = av_clipl_int32(t * (int64_t)cng_filt[0] >> 16);

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
        val = (seg2 - 1) * 16 + val;
        if (t >= y)
            val++;
    } else {
        t = seg * 32 + (val - 1 << seg2);
        t = t * t - x;
        val = (seg2 - 1) * 16 + val;
        if (t >= y)
            val--;
    }

    return val;
}

static void generate_noise(G723_1_ChannelContext *p)
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
            signs[i * 11 + j] = ((t & 1) * 2 - 1)  * (1 << 14);
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
        ff_g723_1_gen_acb_excitation(vector_ptr, vector_ptr,
                                     p->pitch_lag[i >> 1], &p->subframe[i],
                                     p->cur_rate);
        ff_g723_1_gen_acb_excitation(vector_ptr + SUBFRAME_LEN,
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
               t      = vector_ptr[j] * (1 << -shift);
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
           x *= 1 << shift;
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
    G723_1_Context *s  = avctx->priv_data;
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

    if (buf_size < frame_size[dec_mode] * avctx->channels) {
        if (buf_size)
            av_log(avctx, AV_LOG_WARNING,
                   "Expected %d bytes, got %d - skipping packet\n",
                   frame_size[dec_mode], buf_size);
        *got_frame_ptr = 0;
        return buf_size;
    }

    frame->nb_samples = FRAME_LEN;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (int ch = 0; ch < avctx->channels; ch++) {
        G723_1_ChannelContext *p = &s->ch[ch];
        int16_t *audio = p->audio;

        if (unpack_bitstream(p, buf, buf_size) < 0) {
            bad_frame = 1;
            if (p->past_frame_type == ACTIVE_FRAME)
                p->cur_frame_type = ACTIVE_FRAME;
            else
                p->cur_frame_type = UNTRANSMITTED_FRAME;
        }

        out = (int16_t *)frame->extended_data[ch];

        if (p->cur_frame_type == ACTIVE_FRAME) {
            if (!bad_frame)
                p->erased_frames = 0;
            else if (p->erased_frames != 3)
                p->erased_frames++;

            ff_g723_1_inverse_quant(cur_lsp, p->prev_lsp, p->lsp_index, bad_frame);
            ff_g723_1_lsp_interpolate(lpc, cur_lsp, p->prev_lsp);

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
                    ff_g723_1_gen_acb_excitation(acb_vector,
                                                 &p->excitation[SUBFRAME_LEN * i],
                                                 p->pitch_lag[i >> 1],
                                                 &p->subframe[i], p->cur_rate);
                    /* Get the total excitation */
                    for (j = 0; j < SUBFRAME_LEN; j++) {
                        int v = av_clip_int16(vector_ptr[j] * 2);
                        vector_ptr[j] = av_clip_int16(v + acb_vector[j]);
                    }
                    vector_ptr += SUBFRAME_LEN;
                }

                vector_ptr = p->excitation + PITCH_MAX;

                p->interp_index = comp_interp_index(p, p->pitch_lag[1],
                                                    &p->sid_gain, &p->cur_gain);

                /* Perform pitch postfiltering */
                if (s->postfilter) {
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
                ff_g723_1_inverse_quant(p->sid_lsp, p->prev_lsp, p->lsp_index, 0);
            } else if (p->past_frame_type == ACTIVE_FRAME) {
                p->sid_gain = estimate_sid_gain(p);
            }

            if (p->past_frame_type == ACTIVE_FRAME)
                p->cur_gain = p->sid_gain;
            else
                p->cur_gain = (p->cur_gain * 7 + p->sid_gain) >> 3;
            generate_noise(p);
            ff_g723_1_lsp_interpolate(lpc, p->sid_lsp, p->prev_lsp);
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

        if (s->postfilter) {
            formant_postfilter(p, lpc, p->audio, out);
        } else { // if output is not postfiltered it should be scaled by 2
            for (i = 0; i < FRAME_LEN; i++)
                out[i] = av_clip_int16(p->audio[LPC_ORDER + i] << 1);
        }
    }

    *got_frame_ptr = 1;

    return frame_size[dec_mode] * avctx->channels;
}

#define OFFSET(x) offsetof(G723_1_Context, x)
#define AD     AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[] = {
    { "postfilter", "enable postfilter", OFFSET(postfilter), AV_OPT_TYPE_BOOL,
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
    .long_name      = NULL_IF_CONFIG_SMALL("G.723.1"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_G723_1,
    .priv_data_size = sizeof(G723_1_Context),
    .init           = g723_1_decode_init,
    .decode         = g723_1_decode_frame,
    .capabilities   = AV_CODEC_CAP_SUBFRAMES | AV_CODEC_CAP_DR1,
    .priv_class     = &g723_1dec_class,
};
