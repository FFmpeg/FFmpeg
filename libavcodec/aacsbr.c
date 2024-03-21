/*
 * AAC Spectral Band Replication decoding functions
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2009-2010 Alex Converse <alex.converse@gmail.com>
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
 * AAC Spectral Band Replication decoding functions
 * @author Robert Swain ( rob opendot cl )
 */
#define USE_FIXED 0

#include "aac.h"
#include "sbr.h"
#include "aacsbr.h"
#include "aacsbrdata.h"
#include "aacps.h"
#include "sbrdsp.h"
#include "libavutil/internal.h"
#include "libavutil/intfloat.h"
#include "libavutil/libm.h"
#include "libavutil/avassert.h"
#include "libavutil/mem_internal.h"

#include <stdint.h>
#include <float.h>
#include <math.h>

/**
 * 2^(x) for integer x
 * @return correctly rounded float
 */
static av_always_inline float exp2fi(int x) {
    /* Normal range */
    if (-126 <= x && x <= 128)
        return av_int2float((x+127) << 23);
    /* Too large */
    else if (x > 128)
        return INFINITY;
    /* Subnormal numbers */
    else if (x > -150)
        return av_int2float(1 << (x+149));
    /* Negligibly small */
    else
        return 0;
}

static void aacsbr_func_ptr_init(AACSBRContext *c);

static void make_bands(int16_t* bands, int start, int stop, int num_bands)
{
    int k, previous, present;
    float base, prod;

    base = powf((float)stop / start, 1.0f / num_bands);
    prod = start;
    previous = start;

    for (k = 0; k < num_bands-1; k++) {
        prod *= base;
        present  = lrintf(prod);
        bands[k] = present - previous;
        previous = present;
    }
    bands[num_bands-1] = stop - previous;
}

/// Dequantization and stereo decoding (14496-3 sp04 p203)
static void sbr_dequant(SpectralBandReplication *sbr, int id_aac)
{
    int k, e;
    int ch;
    static const double exp2_tab[2] = {1, M_SQRT2};
    if (id_aac == TYPE_CPE && sbr->bs_coupling) {
        int pan_offset = sbr->data[0].bs_amp_res ? 12 : 24;
        for (e = 1; e <= sbr->data[0].bs_num_env; e++) {
            for (k = 0; k < sbr->n[sbr->data[0].bs_freq_res[e]]; k++) {
                float temp1, temp2, fac;
                if (sbr->data[0].bs_amp_res) {
                    temp1 = exp2fi(sbr->data[0].env_facs_q[e][k] + 7);
                    temp2 = exp2fi(pan_offset - sbr->data[1].env_facs_q[e][k]);
                }
                else {
                    temp1 = exp2fi((sbr->data[0].env_facs_q[e][k]>>1) + 7) *
                            exp2_tab[sbr->data[0].env_facs_q[e][k] & 1];
                    temp2 = exp2fi((pan_offset - sbr->data[1].env_facs_q[e][k])>>1) *
                            exp2_tab[(pan_offset - sbr->data[1].env_facs_q[e][k]) & 1];
                }
                if (temp1 > 1E20) {
                    av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                    temp1 = 1;
                }
                fac   = temp1 / (1.0f + temp2);
                sbr->data[0].env_facs[e][k] = fac;
                sbr->data[1].env_facs[e][k] = fac * temp2;
            }
        }
        for (e = 1; e <= sbr->data[0].bs_num_noise; e++) {
            for (k = 0; k < sbr->n_q; k++) {
                float temp1 = exp2fi(NOISE_FLOOR_OFFSET - sbr->data[0].noise_facs_q[e][k] + 1);
                float temp2 = exp2fi(12 - sbr->data[1].noise_facs_q[e][k]);
                float fac;
                av_assert0(temp1 <= 1E20);
                fac = temp1 / (1.0f + temp2);
                sbr->data[0].noise_facs[e][k] = fac;
                sbr->data[1].noise_facs[e][k] = fac * temp2;
            }
        }
    } else { // SCE or one non-coupled CPE
        for (ch = 0; ch < (id_aac == TYPE_CPE) + 1; ch++) {
            for (e = 1; e <= sbr->data[ch].bs_num_env; e++)
                for (k = 0; k < sbr->n[sbr->data[ch].bs_freq_res[e]]; k++){
                    if (sbr->data[ch].bs_amp_res)
                        sbr->data[ch].env_facs[e][k] = exp2fi(sbr->data[ch].env_facs_q[e][k] + 6);
                    else
                        sbr->data[ch].env_facs[e][k] = exp2fi((sbr->data[ch].env_facs_q[e][k]>>1) + 6)
                                                       * exp2_tab[sbr->data[ch].env_facs_q[e][k] & 1];
                    if (sbr->data[ch].env_facs[e][k] > 1E20) {
                        av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                        sbr->data[ch].env_facs[e][k] = 1;
                    }
                }

            for (e = 1; e <= sbr->data[ch].bs_num_noise; e++)
                for (k = 0; k < sbr->n_q; k++)
                    sbr->data[ch].noise_facs[e][k] =
                        exp2fi(NOISE_FLOOR_OFFSET - sbr->data[ch].noise_facs_q[e][k]);
        }
    }
}

/** High Frequency Generation (14496-3 sp04 p214+) and Inverse Filtering
 * (14496-3 sp04 p214)
 * Warning: This routine does not seem numerically stable.
 */
static void sbr_hf_inverse_filter(SBRDSPContext *dsp,
                                  float (*alpha0)[2], float (*alpha1)[2],
                                  const float X_low[32][40][2], int k0)
{
    int k;
    for (k = 0; k < k0; k++) {
        LOCAL_ALIGNED_16(float, phi, [3], [2][2]);
        float dk;

        dsp->autocorrelate(X_low[k], phi);

        dk =  phi[2][1][0] * phi[1][0][0] -
             (phi[1][1][0] * phi[1][1][0] + phi[1][1][1] * phi[1][1][1]) / 1.000001f;

        if (!dk) {
            alpha1[k][0] = 0;
            alpha1[k][1] = 0;
        } else {
            float temp_real, temp_im;
            temp_real = phi[0][0][0] * phi[1][1][0] -
                        phi[0][0][1] * phi[1][1][1] -
                        phi[0][1][0] * phi[1][0][0];
            temp_im   = phi[0][0][0] * phi[1][1][1] +
                        phi[0][0][1] * phi[1][1][0] -
                        phi[0][1][1] * phi[1][0][0];

            alpha1[k][0] = temp_real / dk;
            alpha1[k][1] = temp_im   / dk;
        }

        if (!phi[1][0][0]) {
            alpha0[k][0] = 0;
            alpha0[k][1] = 0;
        } else {
            float temp_real, temp_im;
            temp_real = phi[0][0][0] + alpha1[k][0] * phi[1][1][0] +
                                       alpha1[k][1] * phi[1][1][1];
            temp_im   = phi[0][0][1] + alpha1[k][1] * phi[1][1][0] -
                                       alpha1[k][0] * phi[1][1][1];

            alpha0[k][0] = -temp_real / phi[1][0][0];
            alpha0[k][1] = -temp_im   / phi[1][0][0];
        }

        if (alpha1[k][0] * alpha1[k][0] + alpha1[k][1] * alpha1[k][1] >= 16.0f ||
           alpha0[k][0] * alpha0[k][0] + alpha0[k][1] * alpha0[k][1] >= 16.0f) {
            alpha1[k][0] = 0;
            alpha1[k][1] = 0;
            alpha0[k][0] = 0;
            alpha0[k][1] = 0;
        }
    }
}

/// Chirp Factors (14496-3 sp04 p214)
static void sbr_chirp(SpectralBandReplication *sbr, SBRData *ch_data)
{
    int i;
    float new_bw;
    static const float bw_tab[] = { 0.0f, 0.75f, 0.9f, 0.98f };

    for (i = 0; i < sbr->n_q; i++) {
        if (ch_data->bs_invf_mode[0][i] + ch_data->bs_invf_mode[1][i] == 1) {
            new_bw = 0.6f;
        } else
            new_bw = bw_tab[ch_data->bs_invf_mode[0][i]];

        if (new_bw < ch_data->bw_array[i]) {
            new_bw = 0.75f    * new_bw + 0.25f    * ch_data->bw_array[i];
        } else
            new_bw = 0.90625f * new_bw + 0.09375f * ch_data->bw_array[i];
        ch_data->bw_array[i] = new_bw < 0.015625f ? 0.0f : new_bw;
    }
}

/**
 * Calculation of levels of additional HF signal components (14496-3 sp04 p219)
 * and Calculation of gain (14496-3 sp04 p219)
 */
static void sbr_gain_calc(SpectralBandReplication *sbr,
                          SBRData *ch_data, const int e_a[2])
{
    int e, k, m;
    // max gain limits : -3dB, 0dB, 3dB, inf dB (limiter off)
    static const float limgain[4] = { 0.70795, 1.0, 1.41254, 10000000000 };

    for (e = 0; e < ch_data->bs_num_env; e++) {
        int delta = !((e == e_a[1]) || (e == e_a[0]));
        for (k = 0; k < sbr->n_lim; k++) {
            float gain_boost, gain_max;
            float sum[2] = { 0.0f, 0.0f };
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                const float temp = sbr->e_origmapped[e][m] / (1.0f + sbr->q_mapped[e][m]);
                sbr->q_m[e][m] = sqrtf(temp * sbr->q_mapped[e][m]);
                sbr->s_m[e][m] = sqrtf(temp * ch_data->s_indexmapped[e + 1][m]);
                if (!sbr->s_mapped[e][m]) {
                    sbr->gain[e][m] = sqrtf(sbr->e_origmapped[e][m] /
                                            ((1.0f + sbr->e_curr[e][m]) *
                                             (1.0f + sbr->q_mapped[e][m] * delta)));
                } else {
                    sbr->gain[e][m] = sqrtf(sbr->e_origmapped[e][m] * sbr->q_mapped[e][m] /
                                            ((1.0f + sbr->e_curr[e][m]) *
                                             (1.0f + sbr->q_mapped[e][m])));
                }
                sbr->gain[e][m] += FLT_MIN;
            }
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] += sbr->e_origmapped[e][m];
                sum[1] += sbr->e_curr[e][m];
            }
            gain_max = limgain[sbr->bs_limiter_gains] * sqrtf((FLT_EPSILON + sum[0]) / (FLT_EPSILON + sum[1]));
            gain_max = FFMIN(100000.f, gain_max);
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                float q_m_max   = sbr->q_m[e][m] * gain_max / sbr->gain[e][m];
                sbr->q_m[e][m]  = FFMIN(sbr->q_m[e][m], q_m_max);
                sbr->gain[e][m] = FFMIN(sbr->gain[e][m], gain_max);
            }
            sum[0] = sum[1] = 0.0f;
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] += sbr->e_origmapped[e][m];
                sum[1] += sbr->e_curr[e][m] * sbr->gain[e][m] * sbr->gain[e][m]
                          + sbr->s_m[e][m] * sbr->s_m[e][m]
                          + (delta && !sbr->s_m[e][m]) * sbr->q_m[e][m] * sbr->q_m[e][m];
            }
            gain_boost = sqrtf((FLT_EPSILON + sum[0]) / (FLT_EPSILON + sum[1]));
            gain_boost = FFMIN(1.584893192f, gain_boost);
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sbr->gain[e][m] *= gain_boost;
                sbr->q_m[e][m]  *= gain_boost;
                sbr->s_m[e][m]  *= gain_boost;
            }
        }
    }
}

/// Assembling HF Signals (14496-3 sp04 p220)
static void sbr_hf_assemble(float Y1[38][64][2],
                            const float X_high[64][40][2],
                            SpectralBandReplication *sbr, SBRData *ch_data,
                            const int e_a[2])
{
    int e, i, j, m;
    const int h_SL = 4 * !sbr->bs_smoothing_mode;
    const int kx = sbr->kx[1];
    const int m_max = sbr->m[1];
    static const float h_smooth[5] = {
        0.33333333333333,
        0.30150283239582,
        0.21816949906249,
        0.11516383427084,
        0.03183050093751,
    };
    float (*g_temp)[48] = ch_data->g_temp, (*q_temp)[48] = ch_data->q_temp;
    int indexnoise = ch_data->f_indexnoise;
    int indexsine  = ch_data->f_indexsine;

    if (sbr->reset) {
        for (i = 0; i < h_SL; i++) {
            memcpy(g_temp[i + 2*ch_data->t_env[0]], sbr->gain[0], m_max * sizeof(sbr->gain[0][0]));
            memcpy(q_temp[i + 2*ch_data->t_env[0]], sbr->q_m[0],  m_max * sizeof(sbr->q_m[0][0]));
        }
    } else if (h_SL) {
        for (i = 0; i < 4; i++) {
            memcpy(g_temp[i + 2 * ch_data->t_env[0]],
                   g_temp[i + 2 * ch_data->t_env_num_env_old],
                   sizeof(g_temp[0]));
            memcpy(q_temp[i + 2 * ch_data->t_env[0]],
                   q_temp[i + 2 * ch_data->t_env_num_env_old],
                   sizeof(q_temp[0]));
        }
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            memcpy(g_temp[h_SL + i], sbr->gain[e], m_max * sizeof(sbr->gain[0][0]));
            memcpy(q_temp[h_SL + i], sbr->q_m[e],  m_max * sizeof(sbr->q_m[0][0]));
        }
    }

    for (e = 0; e < ch_data->bs_num_env; e++) {
        for (i = 2 * ch_data->t_env[e]; i < 2 * ch_data->t_env[e + 1]; i++) {
            LOCAL_ALIGNED_16(float, g_filt_tab, [48]);
            LOCAL_ALIGNED_16(float, q_filt_tab, [48]);
            float *g_filt, *q_filt;

            if (h_SL && e != e_a[0] && e != e_a[1]) {
                g_filt = g_filt_tab;
                q_filt = q_filt_tab;
                for (m = 0; m < m_max; m++) {
                    const int idx1 = i + h_SL;
                    g_filt[m] = 0.0f;
                    q_filt[m] = 0.0f;
                    for (j = 0; j <= h_SL; j++) {
                        g_filt[m] += g_temp[idx1 - j][m] * h_smooth[j];
                        q_filt[m] += q_temp[idx1 - j][m] * h_smooth[j];
                    }
                }
            } else {
                g_filt = g_temp[i + h_SL];
                q_filt = q_temp[i];
            }

            sbr->dsp.hf_g_filt(Y1[i] + kx, X_high + kx, g_filt, m_max,
                               i + ENVELOPE_ADJUSTMENT_OFFSET);

            if (e != e_a[0] && e != e_a[1]) {
                sbr->dsp.hf_apply_noise[indexsine](Y1[i] + kx, sbr->s_m[e],
                                                   q_filt, indexnoise,
                                                   kx, m_max);
            } else {
                int idx = indexsine&1;
                int A = (1-((indexsine+(kx & 1))&2));
                int B = (A^(-idx)) + idx;
                float *out = &Y1[i][kx][idx];
                float *in  = sbr->s_m[e];
                for (m = 0; m+1 < m_max; m+=2) {
                    out[2*m  ] += in[m  ] * A;
                    out[2*m+2] += in[m+1] * B;
                }
                if(m_max&1)
                    out[2*m  ] += in[m  ] * A;
            }
            indexnoise = (indexnoise + m_max) & 0x1ff;
            indexsine = (indexsine + 1) & 3;
        }
    }
    ch_data->f_indexnoise = indexnoise;
    ch_data->f_indexsine  = indexsine;
}

#include "aacsbr_template.c"
