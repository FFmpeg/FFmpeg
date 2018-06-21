/*
 * SIPR decoder for the 16k mode
 *
 * Copyright (c) 2008 Vladimir Voroshilov
 * Copyright (c) 2009 Vitor Sessak
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

#include <math.h>

#include "sipr.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/float_dsp.h"
#include "libavutil/mathematics.h"
#include "lsp.h"
#include "acelp_vectors.h"
#include "acelp_pitch_delay.h"
#include "acelp_filters.h"
#include "celp_filters.h"

#include "sipr16kdata.h"

/**
 * Convert an lsf vector into an lsp vector.
 *
 * @param lsf               input lsf vector
 * @param lsp               output lsp vector
 */
static void lsf2lsp(const float *lsf, double *lsp)
{
    int i;

    for (i = 0; i < LP_FILTER_ORDER_16k; i++)
        lsp[i] = cosf(lsf[i]);
}

static void dequant(float *out, const int *idx, const float * const cbs[])
{
    int i;

    for (i = 0; i < 4; i++)
        memcpy(out + 3*i, cbs[i] + 3*idx[i], 3*sizeof(float));

    memcpy(out + 12, cbs[4] + 4*idx[4], 4*sizeof(float));
}

static void lsf_decode_fp_16k(float* lsf_history, float* isp_new,
                              const int* parm, int ma_pred)
{
    int i;
    float isp_q[LP_FILTER_ORDER_16k];

    dequant(isp_q, parm, lsf_codebooks_16k);

    for (i = 0; i < LP_FILTER_ORDER_16k; i++) {
        isp_new[i] = (1 - qu[ma_pred]) * isp_q[i]
                    +     qu[ma_pred]  * lsf_history[i]
                    + mean_lsf_16k[i];
    }

    memcpy(lsf_history, isp_q, LP_FILTER_ORDER_16k * sizeof(float));
}

static int dec_delay3_1st(int index)
{
    if (index < 390) {
        return index + 88;
    } else
        return 3 * index - 690;
}

static int dec_delay3_2nd(int index, int pit_min, int pit_max,
                          int pitch_lag_prev)
{
    if (index < 62) {
        int pitch_delay_min = av_clip(pitch_lag_prev - 10,
                                      pit_min, pit_max - 19);
        return 3 * pitch_delay_min + index - 2;
    } else
        return 3 * pitch_lag_prev;
}

static void postfilter(float *out_data, float* synth, float* iir_mem,
                       float* filt_mem[2], float* mem_preemph)
{
    float buf[30 + LP_FILTER_ORDER_16k];
    float *tmpbuf = buf + LP_FILTER_ORDER_16k;
    float s;
    int i;

    for (i = 0; i < LP_FILTER_ORDER_16k; i++)
        filt_mem[0][i] = iir_mem[i] * ff_pow_0_5[i];

    memcpy(tmpbuf - LP_FILTER_ORDER_16k, mem_preemph,
           LP_FILTER_ORDER_16k*sizeof(*buf));

    ff_celp_lp_synthesis_filterf(tmpbuf, filt_mem[1], synth, 30,
                                 LP_FILTER_ORDER_16k);

    memcpy(synth - LP_FILTER_ORDER_16k, mem_preemph,
           LP_FILTER_ORDER_16k * sizeof(*synth));

    ff_celp_lp_synthesis_filterf(synth, filt_mem[0], synth, 30,
                                 LP_FILTER_ORDER_16k);

    memcpy(out_data + 30 - LP_FILTER_ORDER_16k,
           synth    + 30 - LP_FILTER_ORDER_16k,
           LP_FILTER_ORDER_16k * sizeof(*synth));

    ff_celp_lp_synthesis_filterf(out_data + 30, filt_mem[0],
                                 synth + 30, 2 * L_SUBFR_16k - 30,
                                 LP_FILTER_ORDER_16k);


    memcpy(mem_preemph, out_data + 2*L_SUBFR_16k - LP_FILTER_ORDER_16k,
           LP_FILTER_ORDER_16k * sizeof(*synth));

    FFSWAP(float *, filt_mem[0], filt_mem[1]);
    for (i = 0, s = 0; i < 30; i++, s += 1.0/30)
        out_data[i] = tmpbuf[i] + s * (synth[i] - tmpbuf[i]);
}

/**
 * Floating point version of ff_acelp_lp_decode().
 */
static void acelp_lp_decodef(float *lp_1st, float *lp_2nd,
                             const double *lsp_2nd, const double *lsp_prev)
{
    double lsp_1st[LP_FILTER_ORDER_16k];
    int i;

    /* LSP values for first subframe (3.2.5 of G.729, Equation 24) */
    for (i = 0; i < LP_FILTER_ORDER_16k; i++)
        lsp_1st[i] = (lsp_2nd[i] + lsp_prev[i]) * 0.5;

    ff_acelp_lspd2lpc(lsp_1st, lp_1st, LP_FILTER_ORDER_16k >> 1);

    /* LSP values for second subframe (3.2.5 of G.729) */
    ff_acelp_lspd2lpc(lsp_2nd, lp_2nd, LP_FILTER_ORDER_16k >> 1);
}

/**
 * Floating point version of ff_acelp_decode_gain_code().
 */
static float acelp_decode_gain_codef(float gain_corr_factor, const float *fc_v,
                                     float mr_energy, const float *quant_energy,
                                     const float *ma_prediction_coeff,
                                     int subframe_size, int ma_pred_order)
{
    mr_energy += avpriv_scalarproduct_float_c(quant_energy, ma_prediction_coeff,
                                              ma_pred_order);

    mr_energy = gain_corr_factor * exp(M_LN10 / 20. * mr_energy) /
        sqrt((0.01 + avpriv_scalarproduct_float_c(fc_v, fc_v, subframe_size)));
    return mr_energy;
}

#define DIVIDE_BY_3(x) ((x) * 10923 >> 15)

void ff_sipr_decode_frame_16k(SiprContext *ctx, SiprParameters *params,
                              float *out_data)
{
    int frame_size = SUBFRAME_COUNT_16k * L_SUBFR_16k;
    float *synth = ctx->synth_buf + LP_FILTER_ORDER_16k;
    float lsf_new[LP_FILTER_ORDER_16k];
    double lsp_new[LP_FILTER_ORDER_16k];
    float Az[2][LP_FILTER_ORDER_16k];
    float fixed_vector[L_SUBFR_16k];
    float pitch_fac, gain_code;

    int i;
    int pitch_delay_3x;

    float *excitation = ctx->excitation + 292;

    lsf_decode_fp_16k(ctx->lsf_history, lsf_new, params->vq_indexes,
                      params->ma_pred_switch);

    ff_set_min_dist_lsf(lsf_new, LSFQ_DIFF_MIN / 2, LP_FILTER_ORDER_16k);

    lsf2lsp(lsf_new, lsp_new);

    acelp_lp_decodef(Az[0], Az[1], lsp_new, ctx->lsp_history_16k);

    memcpy(ctx->lsp_history_16k, lsp_new, LP_FILTER_ORDER_16k * sizeof(double));

    memcpy(synth - LP_FILTER_ORDER_16k, ctx->synth,
           LP_FILTER_ORDER_16k * sizeof(*synth));

    for (i = 0; i < SUBFRAME_COUNT_16k; i++) {
        int i_subfr = i * L_SUBFR_16k;
        AMRFixed f;
        float gain_corr_factor;
        int pitch_delay_int;
        int pitch_delay_frac;

        if (!i) {
            pitch_delay_3x = dec_delay3_1st(params->pitch_delay[i]);
        } else
            pitch_delay_3x = dec_delay3_2nd(params->pitch_delay[i],
                                            PITCH_MIN, PITCH_MAX,
                                            ctx->pitch_lag_prev);

        pitch_fac = gain_pitch_cb_16k[params->gp_index[i]];
        f.pitch_fac = FFMIN(pitch_fac, 1.0);
        f.pitch_lag = DIVIDE_BY_3(pitch_delay_3x+1);
        ctx->pitch_lag_prev = f.pitch_lag;

        pitch_delay_int  = DIVIDE_BY_3(pitch_delay_3x + 2);
        pitch_delay_frac = pitch_delay_3x + 2 - 3*pitch_delay_int;

        ff_acelp_interpolatef(&excitation[i_subfr],
                              &excitation[i_subfr] - pitch_delay_int + 1,
                              sinc_win, 3, pitch_delay_frac + 1,
                              LP_FILTER_ORDER, L_SUBFR_16k);


        memset(fixed_vector, 0, sizeof(fixed_vector));

        ff_decode_10_pulses_35bits(params->fc_indexes[i], &f,
                                   ff_fc_4pulses_8bits_tracks_13, 5, 4);

        ff_set_fixed_vector(fixed_vector, &f, 1.0, L_SUBFR_16k);

        gain_corr_factor = gain_cb_16k[params->gc_index[i]];
        gain_code = gain_corr_factor *
            acelp_decode_gain_codef(sqrt(L_SUBFR_16k), fixed_vector,
                                    19.0 - 15.0/(0.05*M_LN10/M_LN2),
                                    pred_16k, ctx->energy_history,
                                    L_SUBFR_16k, 2);

        ctx->energy_history[1] = ctx->energy_history[0];
        ctx->energy_history[0] = 20.0 * log10f(gain_corr_factor);

        ff_weighted_vector_sumf(&excitation[i_subfr], &excitation[i_subfr],
                                fixed_vector, pitch_fac,
                                gain_code, L_SUBFR_16k);

        ff_celp_lp_synthesis_filterf(synth + i_subfr, Az[i],
                                     &excitation[i_subfr], L_SUBFR_16k,
                                     LP_FILTER_ORDER_16k);

    }
    memcpy(ctx->synth, synth + frame_size - LP_FILTER_ORDER_16k,
           LP_FILTER_ORDER_16k * sizeof(*synth));

    memmove(ctx->excitation, ctx->excitation + 2 * L_SUBFR_16k,
            (L_INTERPOL+PITCH_MAX) * sizeof(float));

    postfilter(out_data, synth, ctx->iir_mem, ctx->filt_mem, ctx->mem_preemph);

    memcpy(ctx->iir_mem, Az[1], LP_FILTER_ORDER_16k * sizeof(float));
}

av_cold void ff_sipr_init_16k(SiprContext *ctx)
{
    int i;

    for (i = 0; i < LP_FILTER_ORDER_16k; i++)
        ctx->lsp_history_16k[i] = cos((i + 1) * M_PI/(LP_FILTER_ORDER_16k + 1));

    ctx->filt_mem[0] = ctx->filt_buf[0];
    ctx->filt_mem[1] = ctx->filt_buf[1];

    ctx->pitch_lag_prev = 180;
}
