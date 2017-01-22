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
 *
 * Note: Rounding-to-nearest used unless otherwise stated
 *
 */

#define USE_FIXED 1

#include "aac.h"
#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/intfloat.h"
#include "sbrdsp.h"

static SoftFloat sbr_sum_square_c(int (*x)[2], int n)
{
    SoftFloat ret;
    int64_t accu = 0;
    int i, nz, round;

    for (i = 0; i < n; i += 2) {
        // Larger values are inavlid and could cause overflows of accu.
        av_assert2(FFABS(x[i + 0][0]) >> 29 == 0);
        accu += (int64_t)x[i + 0][0] * x[i + 0][0];
        av_assert2(FFABS(x[i + 0][1]) >> 29 == 0);
        accu += (int64_t)x[i + 0][1] * x[i + 0][1];
        av_assert2(FFABS(x[i + 1][0]) >> 29 == 0);
        accu += (int64_t)x[i + 1][0] * x[i + 1][0];
        av_assert2(FFABS(x[i + 1][1]) >> 29 == 0);
        accu += (int64_t)x[i + 1][1] * x[i + 1][1];
    }

    i = (int)(accu >> 32);
    if (i == 0) {
        nz = 1;
    } else {
        nz = 0;
        while (FFABS(i) < 0x40000000) {
            i <<= 1;
            nz++;
        }
        nz = 32 - nz;
    }

    round = 1 << (nz-1);
    i = (int)((accu + round) >> nz);
    i >>= 1;
    ret = av_int2sf(i, 15 - nz);

    return ret;
}

static void sbr_neg_odd_64_c(int *x)
{
    int i;
    for (i = 1; i < 64; i += 2)
        x[i] = -x[i];
}

static void sbr_qmf_pre_shuffle_c(int *z)
{
    int k;
    z[64] = z[0];
    z[65] = z[1];
    for (k = 1; k < 32; k++) {
        z[64+2*k  ] = -z[64 - k];
        z[64+2*k+1] =  z[ k + 1];
    }
}

static void sbr_qmf_post_shuffle_c(int W[32][2], const int *z)
{
    int k;
    for (k = 0; k < 32; k++) {
        W[k][0] = -z[63-k];
        W[k][1] = z[k];
    }
}

static void sbr_qmf_deint_neg_c(int *v, const int *src)
{
    int i;
    for (i = 0; i < 32; i++) {
        v[     i] = ( src[63 - 2*i    ] + 0x10) >> 5;
        v[63 - i] = (-src[63 - 2*i - 1] + 0x10) >> 5;
    }
}

static av_always_inline SoftFloat autocorr_calc(int64_t accu)
{
        int nz, mant, expo, round;
        int i = (int)(accu >> 32);
        if (i == 0) {
            nz = 1;
        } else {
            nz = 0;
            while (FFABS(i) < 0x40000000) {
                i <<= 1;
                nz++;
            }
            nz = 32-nz;
        }

        round = 1 << (nz-1);
        mant = (int)((accu + round) >> nz);
        mant = (mant + 0x40)>>7;
        mant <<= 6;
        expo = nz + 15;
        return av_int2sf(mant, 30 - expo);
}

static av_always_inline void autocorrelate(const int x[40][2], SoftFloat phi[3][2][2], int lag)
{
    int i;
    int64_t real_sum, imag_sum;
    int64_t accu_re = 0, accu_im = 0;

    if (lag) {
        for (i = 1; i < 38; i++) {
            accu_re += (int64_t)x[i][0] * x[i+lag][0];
            accu_re += (int64_t)x[i][1] * x[i+lag][1];
            accu_im += (int64_t)x[i][0] * x[i+lag][1];
            accu_im -= (int64_t)x[i][1] * x[i+lag][0];
        }

        real_sum = accu_re;
        imag_sum = accu_im;

        accu_re += (int64_t)x[ 0][0] * x[lag][0];
        accu_re += (int64_t)x[ 0][1] * x[lag][1];
        accu_im += (int64_t)x[ 0][0] * x[lag][1];
        accu_im -= (int64_t)x[ 0][1] * x[lag][0];

        phi[2-lag][1][0] = autocorr_calc(accu_re);
        phi[2-lag][1][1] = autocorr_calc(accu_im);

        if (lag == 1) {
            accu_re = real_sum;
            accu_im = imag_sum;
            accu_re += (int64_t)x[38][0] * x[39][0];
            accu_re += (int64_t)x[38][1] * x[39][1];
            accu_im += (int64_t)x[38][0] * x[39][1];
            accu_im -= (int64_t)x[38][1] * x[39][0];

            phi[0][0][0] = autocorr_calc(accu_re);
            phi[0][0][1] = autocorr_calc(accu_im);
        }
    } else {
        for (i = 1; i < 38; i++) {
            accu_re += (int64_t)x[i][0] * x[i][0];
            accu_re += (int64_t)x[i][1] * x[i][1];
        }
        real_sum = accu_re;
        accu_re += (int64_t)x[ 0][0] * x[ 0][0];
        accu_re += (int64_t)x[ 0][1] * x[ 0][1];

        phi[2][1][0] = autocorr_calc(accu_re);

        accu_re = real_sum;
        accu_re += (int64_t)x[38][0] * x[38][0];
        accu_re += (int64_t)x[38][1] * x[38][1];

        phi[1][0][0] = autocorr_calc(accu_re);
    }
}

static void sbr_autocorrelate_c(const int x[40][2], SoftFloat phi[3][2][2])
{
    autocorrelate(x, phi, 0);
    autocorrelate(x, phi, 1);
    autocorrelate(x, phi, 2);
}

static void sbr_hf_gen_c(int (*X_high)[2], const int (*X_low)[2],
                       const int alpha0[2], const int alpha1[2],
                       int bw, int start, int end)
{
    int alpha[4];
    int i;
    int64_t accu;

    accu = (int64_t)alpha0[0] * bw;
    alpha[2] = (int)((accu + 0x40000000) >> 31);
    accu = (int64_t)alpha0[1] * bw;
    alpha[3] = (int)((accu + 0x40000000) >> 31);
    accu = (int64_t)bw * bw;
    bw = (int)((accu + 0x40000000) >> 31);
    accu = (int64_t)alpha1[0] * bw;
    alpha[0] = (int)((accu + 0x40000000) >> 31);
    accu = (int64_t)alpha1[1] * bw;
    alpha[1] = (int)((accu + 0x40000000) >> 31);

    for (i = start; i < end; i++) {
        accu  = (int64_t)X_low[i][0] * 0x20000000;
        accu += (int64_t)X_low[i - 2][0] * alpha[0];
        accu -= (int64_t)X_low[i - 2][1] * alpha[1];
        accu += (int64_t)X_low[i - 1][0] * alpha[2];
        accu -= (int64_t)X_low[i - 1][1] * alpha[3];
        X_high[i][0] = (int)((accu + 0x10000000) >> 29);

        accu  = (int64_t)X_low[i][1] * 0x20000000;
        accu += (int64_t)X_low[i - 2][1] * alpha[0];
        accu += (int64_t)X_low[i - 2][0] * alpha[1];
        accu += (int64_t)X_low[i - 1][1] * alpha[2];
        accu += (int64_t)X_low[i - 1][0] * alpha[3];
        X_high[i][1] = (int)((accu + 0x10000000) >> 29);
    }
}

static void sbr_hf_g_filt_c(int (*Y)[2], const int (*X_high)[40][2],
                          const SoftFloat *g_filt, int m_max, intptr_t ixh)
{
    int m, r;
    int64_t accu;

    for (m = 0; m < m_max; m++) {
        r = 1 << (22-g_filt[m].exp);
        accu = (int64_t)X_high[m][ixh][0] * ((g_filt[m].mant + 0x40)>>7);
        Y[m][0] = (int)((accu + r) >> (23-g_filt[m].exp));

        accu = (int64_t)X_high[m][ixh][1] * ((g_filt[m].mant + 0x40)>>7);
        Y[m][1] = (int)((accu + r) >> (23-g_filt[m].exp));
    }
}

static av_always_inline void sbr_hf_apply_noise(int (*Y)[2],
                                                const SoftFloat *s_m,
                                                const SoftFloat *q_filt,
                                                int noise,
                                                int phi_sign0,
                                                int phi_sign1,
                                                int m_max)
{
    int m;

    for (m = 0; m < m_max; m++) {
        int y0 = Y[m][0];
        int y1 = Y[m][1];
        noise = (noise + 1) & 0x1ff;
        if (s_m[m].mant) {
            int shift, round;

            shift = 22 - s_m[m].exp;
            if (shift < 30) {
                round = 1 << (shift-1);
                y0 += (s_m[m].mant * phi_sign0 + round) >> shift;
                y1 += (s_m[m].mant * phi_sign1 + round) >> shift;
            }
        } else {
            int shift, round, tmp;
            int64_t accu;

            shift = 22 - q_filt[m].exp;
            if (shift < 30) {
                round = 1 << (shift-1);

                accu = (int64_t)q_filt[m].mant * ff_sbr_noise_table_fixed[noise][0];
                tmp = (int)((accu + 0x40000000) >> 31);
                y0 += (tmp + round) >> shift;

                accu = (int64_t)q_filt[m].mant * ff_sbr_noise_table_fixed[noise][1];
                tmp = (int)((accu + 0x40000000) >> 31);
                y1 += (tmp + round) >> shift;
            }
        }
        Y[m][0] = y0;
        Y[m][1] = y1;
        phi_sign1 = -phi_sign1;
    }
}

#include "sbrdsp_template.c"
