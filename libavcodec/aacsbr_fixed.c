/*
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the MIPS Technologies, Inc., nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE MIPS TECHNOLOGIES, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE MIPS TECHNOLOGIES, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * AAC Spectral Band Replication decoding functions (fixed-point)
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
 * AAC Spectral Band Replication decoding functions (fixed-point)
 * Note: Rounding-to-nearest used unless otherwise stated
 * @author Robert Swain ( rob opendot cl )
 * @author Stanislav Ocovaj ( stanislav.ocovaj imgtec com )
 */
#define USE_FIXED 1

#include "aac.h"
#include "sbr.h"
#include "aacsbr.h"
#include "aacsbrdata.h"
#include "aacsbr_fixed_tablegen.h"
#include "fft.h"
#include "aacps.h"
#include "sbrdsp.h"
#include "libavutil/internal.h"
#include "libavutil/libm.h"
#include "libavutil/avassert.h"

#include <stdint.h>
#include <float.h>
#include <math.h>

static VLC vlc_sbr[10];
static void aacsbr_func_ptr_init(AACSBRContext *c);
static const int CONST_LN2       = Q31(0.6931471806/256);  // ln(2)/256
static const int CONST_RECIP_LN2 = Q31(0.7213475204);      // 0.5/ln(2)
static const int CONST_076923    = Q31(0.76923076923076923077f);

static const int fixed_log_table[10] =
{
    Q31(1.0/2), Q31(1.0/3), Q31(1.0/4), Q31(1.0/5), Q31(1.0/6),
    Q31(1.0/7), Q31(1.0/8), Q31(1.0/9), Q31(1.0/10), Q31(1.0/11)
};

static int fixed_log(int x)
{
    int i, ret, xpow, tmp;

    ret = x;
    xpow = x;
    for (i=0; i<10; i+=2){
        xpow = (int)(((int64_t)xpow * x + 0x40000000) >> 31);
        tmp = (int)(((int64_t)xpow * fixed_log_table[i] + 0x40000000) >> 31);
        ret -= tmp;

        xpow = (int)(((int64_t)xpow * x + 0x40000000) >> 31);
        tmp = (int)(((int64_t)xpow * fixed_log_table[i+1] + 0x40000000) >> 31);
        ret += tmp;
    }

    return ret;
}

static const int fixed_exp_table[7] =
{
    Q31(1.0/2), Q31(1.0/6), Q31(1.0/24), Q31(1.0/120),
    Q31(1.0/720), Q31(1.0/5040), Q31(1.0/40320)
};

static int fixed_exp(int x)
{
    int i, ret, xpow, tmp;

    ret = 0x800000 + x;
    xpow = x;
    for (i=0; i<7; i++){
        xpow = (int)(((int64_t)xpow * x + 0x400000) >> 23);
        tmp = (int)(((int64_t)xpow * fixed_exp_table[i] + 0x40000000) >> 31);
        ret += tmp;
    }

    return ret;
}

static void make_bands(int16_t* bands, int start, int stop, int num_bands)
{
    int k, previous, present;
    int base, prod, nz = 0;

    base = (stop << 23) / start;
    while (base < 0x40000000){
        base <<= 1;
        nz++;
    }
    base = fixed_log(base - 0x80000000);
    base = (((base + 0x80) >> 8) + (8-nz)*CONST_LN2) / num_bands;
    base = fixed_exp(base);

    previous = start;
    prod = start << 23;

    for (k = 0; k < num_bands-1; k++) {
        prod = (int)(((int64_t)prod * base + 0x400000) >> 23);
        present = (prod + 0x400000) >> 23;
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

    if (id_aac == TYPE_CPE && sbr->bs_coupling) {
        int alpha      = sbr->data[0].bs_amp_res ?  2 :  1;
        int pan_offset = sbr->data[0].bs_amp_res ? 12 : 24;
        for (e = 1; e <= sbr->data[0].bs_num_env; e++) {
            for (k = 0; k < sbr->n[sbr->data[0].bs_freq_res[e]]; k++) {
                SoftFloat temp1, temp2, fac;

                temp1.exp = sbr->data[0].env_facs_q[e][k] * alpha + 14;
                if (temp1.exp & 1)
                  temp1.mant = 759250125;
                else
                  temp1.mant = 0x20000000;
                temp1.exp = (temp1.exp >> 1) + 1;
                if (temp1.exp > 66) { // temp1 > 1E20
                    av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                    temp1 = FLOAT_1;
                }

                temp2.exp = (pan_offset - sbr->data[1].env_facs_q[e][k]) * alpha;
                if (temp2.exp & 1)
                  temp2.mant = 759250125;
                else
                  temp2.mant = 0x20000000;
                temp2.exp = (temp2.exp >> 1) + 1;
                fac   = av_div_sf(temp1, av_add_sf(FLOAT_1, temp2));
                sbr->data[0].env_facs[e][k] = fac;
                sbr->data[1].env_facs[e][k] = av_mul_sf(fac, temp2);
            }
        }
        for (e = 1; e <= sbr->data[0].bs_num_noise; e++) {
            for (k = 0; k < sbr->n_q; k++) {
                SoftFloat temp1, temp2, fac;

                temp1.exp = NOISE_FLOOR_OFFSET - \
                    sbr->data[0].noise_facs_q[e][k] + 2;
                temp1.mant = 0x20000000;
                av_assert0(temp1.exp <= 66);
                temp2.exp = 12 - sbr->data[1].noise_facs_q[e][k] + 1;
                temp2.mant = 0x20000000;
                fac   = av_div_sf(temp1, av_add_sf(FLOAT_1, temp2));
                sbr->data[0].noise_facs[e][k] = fac;
                sbr->data[1].noise_facs[e][k] = av_mul_sf(fac, temp2);
            }
        }
    } else { // SCE or one non-coupled CPE
        for (ch = 0; ch < (id_aac == TYPE_CPE) + 1; ch++) {
            int alpha = sbr->data[ch].bs_amp_res ? 2 : 1;
            for (e = 1; e <= sbr->data[ch].bs_num_env; e++)
                for (k = 0; k < sbr->n[sbr->data[ch].bs_freq_res[e]]; k++){
                    SoftFloat temp1;

                    temp1.exp = alpha * sbr->data[ch].env_facs_q[e][k] + 12;
                    if (temp1.exp & 1)
                        temp1.mant = 759250125;
                    else
                        temp1.mant = 0x20000000;
                    temp1.exp = (temp1.exp >> 1) + 1;
                    if (temp1.exp > 66) { // temp1 > 1E20
                        av_log(NULL, AV_LOG_ERROR, "envelope scalefactor overflow in dequant\n");
                        temp1 = FLOAT_1;
                    }
                    sbr->data[ch].env_facs[e][k] = temp1;
                }
            for (e = 1; e <= sbr->data[ch].bs_num_noise; e++)
                for (k = 0; k < sbr->n_q; k++){
                    sbr->data[ch].noise_facs[e][k].exp = NOISE_FLOOR_OFFSET - \
                        sbr->data[ch].noise_facs_q[e][k] + 1;
                    sbr->data[ch].noise_facs[e][k].mant = 0x20000000;
                }
        }
    }
}

/** High Frequency Generation (14496-3 sp04 p214+) and Inverse Filtering
 * (14496-3 sp04 p214)
 * Warning: This routine does not seem numerically stable.
 */
static void sbr_hf_inverse_filter(SBRDSPContext *dsp,
                                  int (*alpha0)[2], int (*alpha1)[2],
                                  const int X_low[32][40][2], int k0)
{
    int k;
    int shift, round;

    for (k = 0; k < k0; k++) {
        SoftFloat phi[3][2][2];
        SoftFloat a00, a01, a10, a11;
        SoftFloat dk;

        dsp->autocorrelate(X_low[k], phi);

        dk = av_sub_sf(av_mul_sf(phi[2][1][0], phi[1][0][0]),
             av_mul_sf(av_add_sf(av_mul_sf(phi[1][1][0], phi[1][1][0]),
             av_mul_sf(phi[1][1][1], phi[1][1][1])), FLOAT_0999999));

        if (!dk.mant) {
            a10 = FLOAT_0;
            a11 = FLOAT_0;
        } else {
            SoftFloat temp_real, temp_im;
            temp_real = av_sub_sf(av_sub_sf(av_mul_sf(phi[0][0][0], phi[1][1][0]),
                                            av_mul_sf(phi[0][0][1], phi[1][1][1])),
                                  av_mul_sf(phi[0][1][0], phi[1][0][0]));
            temp_im   = av_sub_sf(av_add_sf(av_mul_sf(phi[0][0][0], phi[1][1][1]),
                                            av_mul_sf(phi[0][0][1], phi[1][1][0])),
                                  av_mul_sf(phi[0][1][1], phi[1][0][0]));

            a10 = av_div_sf(temp_real, dk);
            a11 = av_div_sf(temp_im,   dk);
        }

        if (!phi[1][0][0].mant) {
            a00 = FLOAT_0;
            a01 = FLOAT_0;
        } else {
            SoftFloat temp_real, temp_im;
            temp_real = av_add_sf(phi[0][0][0],
                                  av_add_sf(av_mul_sf(a10, phi[1][1][0]),
                                            av_mul_sf(a11, phi[1][1][1])));
            temp_im   = av_add_sf(phi[0][0][1],
                                  av_sub_sf(av_mul_sf(a11, phi[1][1][0]),
                                            av_mul_sf(a10, phi[1][1][1])));

            temp_real.mant = -temp_real.mant;
            temp_im.mant   = -temp_im.mant;
            a00 = av_div_sf(temp_real, phi[1][0][0]);
            a01 = av_div_sf(temp_im,   phi[1][0][0]);
        }

        shift = a00.exp;
        if (shift >= 3)
            alpha0[k][0] = 0x7fffffff;
        else if (shift <= -30)
            alpha0[k][0] = 0;
        else {
            shift = 1-shift;
            if (shift <= 0)
                alpha0[k][0] = a00.mant * (1<<-shift);
            else {
                round = 1 << (shift-1);
                alpha0[k][0] = (a00.mant + round) >> shift;
            }
        }

        shift = a01.exp;
        if (shift >= 3)
            alpha0[k][1] = 0x7fffffff;
        else if (shift <= -30)
            alpha0[k][1] = 0;
        else {
            shift = 1-shift;
            if (shift <= 0)
                alpha0[k][1] = a01.mant * (1<<-shift);
            else {
                round = 1 << (shift-1);
                alpha0[k][1] = (a01.mant + round) >> shift;
            }
        }
        shift = a10.exp;
        if (shift >= 3)
            alpha1[k][0] = 0x7fffffff;
        else if (shift <= -30)
            alpha1[k][0] = 0;
        else {
            shift = 1-shift;
            if (shift <= 0)
                alpha1[k][0] = a10.mant * (1<<-shift);
            else {
                round = 1 << (shift-1);
                alpha1[k][0] = (a10.mant + round) >> shift;
            }
        }

        shift = a11.exp;
        if (shift >= 3)
            alpha1[k][1] = 0x7fffffff;
        else if (shift <= -30)
            alpha1[k][1] = 0;
        else {
            shift = 1-shift;
            if (shift <= 0)
                alpha1[k][1] = a11.mant * (1<<-shift);
            else {
                round = 1 << (shift-1);
                alpha1[k][1] = (a11.mant + round) >> shift;
            }
        }

        shift = (int)(((int64_t)(alpha1[k][0]>>1) * (alpha1[k][0]>>1) + \
                       (int64_t)(alpha1[k][1]>>1) * (alpha1[k][1]>>1) + \
                       0x40000000) >> 31);
        if (shift >= 0x20000000){
            alpha1[k][0] = 0;
            alpha1[k][1] = 0;
            alpha0[k][0] = 0;
            alpha0[k][1] = 0;
        }

        shift = (int)(((int64_t)(alpha0[k][0]>>1) * (alpha0[k][0]>>1) + \
                       (int64_t)(alpha0[k][1]>>1) * (alpha0[k][1]>>1) + \
                       0x40000000) >> 31);
        if (shift >= 0x20000000){
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
    int new_bw;
    static const int bw_tab[] = { 0, 1610612736, 1932735283, 2104533975 };
    int64_t accu;

    for (i = 0; i < sbr->n_q; i++) {
        if (ch_data->bs_invf_mode[0][i] + ch_data->bs_invf_mode[1][i] == 1)
            new_bw = 1288490189;
        else
            new_bw = bw_tab[ch_data->bs_invf_mode[0][i]];

        if (new_bw < ch_data->bw_array[i]){
            accu  = (int64_t)new_bw * 1610612736;
            accu += (int64_t)ch_data->bw_array[i] * 0x20000000;
            new_bw = (int)((accu + 0x40000000) >> 31);
        } else {
            accu  = (int64_t)new_bw * 1946157056;
            accu += (int64_t)ch_data->bw_array[i] * 201326592;
            new_bw = (int)((accu + 0x40000000) >> 31);
        }
        ch_data->bw_array[i] = new_bw < 0x2000000 ? 0 : new_bw;
    }
}

/**
 * Calculation of levels of additional HF signal components (14496-3 sp04 p219)
 * and Calculation of gain (14496-3 sp04 p219)
 */
static void sbr_gain_calc(AACContext *ac, SpectralBandReplication *sbr,
                          SBRData *ch_data, const int e_a[2])
{
    int e, k, m;
    // max gain limits : -3dB, 0dB, 3dB, inf dB (limiter off)
    static const SoftFloat limgain[4] = { { 760155524,  0 }, { 0x20000000,  1 },
                                            { 758351638,  1 }, { 625000000, 34 } };

    for (e = 0; e < ch_data->bs_num_env; e++) {
        int delta = !((e == e_a[1]) || (e == e_a[0]));
        for (k = 0; k < sbr->n_lim; k++) {
            SoftFloat gain_boost, gain_max;
            SoftFloat sum[2];
            sum[0] = sum[1] = FLOAT_0;
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                const SoftFloat temp = av_div_sf(sbr->e_origmapped[e][m],
                                            av_add_sf(FLOAT_1, sbr->q_mapped[e][m]));
                sbr->q_m[e][m] = av_sqrt_sf(av_mul_sf(temp, sbr->q_mapped[e][m]));
                sbr->s_m[e][m] = av_sqrt_sf(av_mul_sf(temp, av_int2sf(ch_data->s_indexmapped[e + 1][m], 0)));
                if (!sbr->s_mapped[e][m]) {
                    if (delta) {
                      sbr->gain[e][m] = av_sqrt_sf(av_div_sf(sbr->e_origmapped[e][m],
                                            av_mul_sf(av_add_sf(FLOAT_1, sbr->e_curr[e][m]),
                                            av_add_sf(FLOAT_1, sbr->q_mapped[e][m]))));
                    } else {
                      sbr->gain[e][m] = av_sqrt_sf(av_div_sf(sbr->e_origmapped[e][m],
                                            av_add_sf(FLOAT_1, sbr->e_curr[e][m])));
                    }
                } else {
                    sbr->gain[e][m] = av_sqrt_sf(
                                        av_div_sf(
                                            av_mul_sf(sbr->e_origmapped[e][m], sbr->q_mapped[e][m]),
                                            av_mul_sf(
                                                av_add_sf(FLOAT_1, sbr->e_curr[e][m]),
                                                av_add_sf(FLOAT_1, sbr->q_mapped[e][m]))));
                }
                sbr->gain[e][m] = av_add_sf(sbr->gain[e][m], FLOAT_MIN);
            }
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] = av_add_sf(sum[0], sbr->e_origmapped[e][m]);
                sum[1] = av_add_sf(sum[1], sbr->e_curr[e][m]);
            }
            gain_max = av_mul_sf(limgain[sbr->bs_limiter_gains],
                            av_sqrt_sf(
                                av_div_sf(
                                    av_add_sf(FLOAT_EPSILON, sum[0]),
                                    av_add_sf(FLOAT_EPSILON, sum[1]))));
            if (av_gt_sf(gain_max, FLOAT_100000))
              gain_max = FLOAT_100000;
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                SoftFloat q_m_max = av_div_sf(
                                        av_mul_sf(sbr->q_m[e][m], gain_max),
                                        sbr->gain[e][m]);
                if (av_gt_sf(sbr->q_m[e][m], q_m_max))
                  sbr->q_m[e][m] = q_m_max;
                if (av_gt_sf(sbr->gain[e][m], gain_max))
                  sbr->gain[e][m] = gain_max;
            }
            sum[0] = sum[1] = FLOAT_0;
            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sum[0] = av_add_sf(sum[0], sbr->e_origmapped[e][m]);
                sum[1] = av_add_sf(sum[1],
                            av_mul_sf(
                                av_mul_sf(sbr->e_curr[e][m],
                                          sbr->gain[e][m]),
                                sbr->gain[e][m]));
                sum[1] = av_add_sf(sum[1],
                            av_mul_sf(sbr->s_m[e][m], sbr->s_m[e][m]));
                if (delta && !sbr->s_m[e][m].mant)
                  sum[1] = av_add_sf(sum[1],
                                av_mul_sf(sbr->q_m[e][m], sbr->q_m[e][m]));
            }
            gain_boost = av_sqrt_sf(
                            av_div_sf(
                                av_add_sf(FLOAT_EPSILON, sum[0]),
                                av_add_sf(FLOAT_EPSILON, sum[1])));
            if (av_gt_sf(gain_boost, FLOAT_1584893192))
              gain_boost = FLOAT_1584893192;

            for (m = sbr->f_tablelim[k] - sbr->kx[1]; m < sbr->f_tablelim[k + 1] - sbr->kx[1]; m++) {
                sbr->gain[e][m] = av_mul_sf(sbr->gain[e][m], gain_boost);
                sbr->q_m[e][m]  = av_mul_sf(sbr->q_m[e][m], gain_boost);
                sbr->s_m[e][m]  = av_mul_sf(sbr->s_m[e][m], gain_boost);
            }
        }
    }
}

/// Assembling HF Signals (14496-3 sp04 p220)
static void sbr_hf_assemble(int Y1[38][64][2],
                            const int X_high[64][40][2],
                            SpectralBandReplication *sbr, SBRData *ch_data,
                            const int e_a[2])
{
    int e, i, j, m;
    const int h_SL = 4 * !sbr->bs_smoothing_mode;
    const int kx = sbr->kx[1];
    const int m_max = sbr->m[1];
    static const SoftFloat h_smooth[5] = {
      { 715827883, -1 },
      { 647472402, -1 },
      { 937030863, -2 },
      { 989249804, -3 },
      { 546843842, -4 },
    };
    SoftFloat (*g_temp)[48] = ch_data->g_temp, (*q_temp)[48] = ch_data->q_temp;
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
            SoftFloat g_filt_tab[48];
            SoftFloat q_filt_tab[48];
            SoftFloat *g_filt, *q_filt;

            if (h_SL && e != e_a[0] && e != e_a[1]) {
                g_filt = g_filt_tab;
                q_filt = q_filt_tab;
                for (m = 0; m < m_max; m++) {
                    const int idx1 = i + h_SL;
                    g_filt[m].mant = g_filt[m].exp = 0;
                    q_filt[m].mant = q_filt[m].exp = 0;
                    for (j = 0; j <= h_SL; j++) {
                        g_filt[m] = av_add_sf(g_filt[m],
                                        av_mul_sf(g_temp[idx1 - j][m],
                                            h_smooth[j]));
                        q_filt[m] = av_add_sf(q_filt[m],
                                        av_mul_sf(q_temp[idx1 - j][m],
                                            h_smooth[j]));
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
                int *out = &Y1[i][kx][idx];
                int shift;
                unsigned round;

                SoftFloat *in  = sbr->s_m[e];
                for (m = 0; m+1 < m_max; m+=2) {
                    int shift2;
                    shift = 22 - in[m  ].exp;
                    shift2= 22 - in[m+1].exp;
                    if (shift < 1 || shift2 < 1) {
                        av_log(NULL, AV_LOG_ERROR, "Overflow in sbr_hf_assemble, shift=%d,%d\n", shift, shift2);
                        return;
                    }
                    if (shift < 32) {
                        round = 1 << (shift-1);
                        out[2*m  ] += (int)(in[m  ].mant * A + round) >> shift;
                    }

                    if (shift2 < 32) {
                        round = 1 << (shift2-1);
                        out[2*m+2] += (int)(in[m+1].mant * B + round) >> shift2;
                    }
                }
                if(m_max&1)
                {
                    shift = 22 - in[m  ].exp;
                    if (shift < 1) {
                        av_log(NULL, AV_LOG_ERROR, "Overflow in sbr_hf_assemble, shift=%d\n", shift);
                        return;
                    } else if (shift < 32) {
                        round = 1 << (shift-1);
                        out[2*m  ] += (int)(in[m  ].mant * A + round) >> shift;
                    }
                }
            }
            indexnoise = (indexnoise + m_max) & 0x1ff;
            indexsine = (indexsine + 1) & 3;
        }
    }
    ch_data->f_indexnoise = indexnoise;
    ch_data->f_indexsine  = indexsine;
}

#include "aacsbr_template.c"
