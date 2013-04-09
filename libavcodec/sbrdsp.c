/*
 * AAC Spectral Band Replication decoding functions
 * Copyright (c) 2008-2009 Robert Swain ( rob opendot cl )
 * Copyright (c) 2009-2010 Alex Converse <alex.converse@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/intfloat.h"
#include "sbrdsp.h"

static void sbr_sum64x5_c(float *z)
{
    int k;
    for (k = 0; k < 64; k++) {
        float f = z[k] + z[k + 64] + z[k + 128] + z[k + 192] + z[k + 256];
        z[k] = f;
    }
}

static float sbr_sum_square_c(float (*x)[2], int n)
{
    float sum0 = 0.0f, sum1 = 0.0f;
    int i;

    for (i = 0; i < n; i += 2)
    {
        sum0 += x[i + 0][0] * x[i + 0][0];
        sum1 += x[i + 0][1] * x[i + 0][1];
        sum0 += x[i + 1][0] * x[i + 1][0];
        sum1 += x[i + 1][1] * x[i + 1][1];
    }

    return sum0 + sum1;
}

static void sbr_neg_odd_64_c(float *x)
{
    union av_intfloat32 *xi = (union av_intfloat32*) x;
    int i;
    for (i = 1; i < 64; i += 4) {
        xi[i + 0].i ^= 1U << 31;
        xi[i + 2].i ^= 1U << 31;
    }
}

static void sbr_qmf_pre_shuffle_c(float *z)
{
    union av_intfloat32 *zi = (union av_intfloat32*) z;
    int k;
    zi[64].i = zi[0].i;
    zi[65].i = zi[1].i;
    for (k = 1; k < 31; k += 2) {
        zi[64 + 2 * k + 0].i = zi[64 - k].i ^ (1U << 31);
        zi[64 + 2 * k + 1].i = zi[ k + 1].i;
        zi[64 + 2 * k + 2].i = zi[63 - k].i ^ (1U << 31);
        zi[64 + 2 * k + 3].i = zi[ k + 2].i;
    }
    zi[64 + 2 * 31 + 0].i = zi[64 - 31].i ^ (1U << 31);
    zi[64 + 2 * 31 + 1].i = zi[31 +  1].i;
}

static void sbr_qmf_post_shuffle_c(float W[32][2], const float *z)
{
    const union av_intfloat32 *zi = (const union av_intfloat32*) z;
    union av_intfloat32 *Wi       = (union av_intfloat32*) W;
    int k;
    for (k = 0; k < 32; k += 2) {
        Wi[2 * k + 0].i = zi[63 - k].i ^ (1U << 31);
        Wi[2 * k + 1].i = zi[ k + 0].i;
        Wi[2 * k + 2].i = zi[62 - k].i ^ (1U << 31);
        Wi[2 * k + 3].i = zi[ k + 1].i;
    }
}

static void sbr_qmf_deint_neg_c(float *v, const float *src)
{
    const union av_intfloat32 *si = (const union av_intfloat32*)src;
    union av_intfloat32 *vi = (union av_intfloat32*)v;
    int i;
    for (i = 0; i < 32; i++) {
        vi[     i].i = si[63 - 2 * i    ].i;
        vi[63 - i].i = si[63 - 2 * i - 1].i ^ (1U << 31);
    }
}

static void sbr_qmf_deint_bfly_c(float *v, const float *src0, const float *src1)
{
    int i;
    for (i = 0; i < 64; i++) {
        v[      i] = src0[i] - src1[63 - i];
        v[127 - i] = src0[i] + src1[63 - i];
    }
}

static av_always_inline void autocorrelate(const float x[40][2],
                                           float phi[3][2][2], int lag)
{
    int i;
    float real_sum = 0.0f;
    float imag_sum = 0.0f;
    if (lag) {
        for (i = 1; i < 38; i++) {
            real_sum += x[i][0] * x[i+lag][0] + x[i][1] * x[i+lag][1];
            imag_sum += x[i][0] * x[i+lag][1] - x[i][1] * x[i+lag][0];
        }
        phi[2-lag][1][0] = real_sum + x[ 0][0] * x[lag][0] + x[ 0][1] * x[lag][1];
        phi[2-lag][1][1] = imag_sum + x[ 0][0] * x[lag][1] - x[ 0][1] * x[lag][0];
        if (lag == 1) {
            phi[0][0][0] = real_sum + x[38][0] * x[39][0] + x[38][1] * x[39][1];
            phi[0][0][1] = imag_sum + x[38][0] * x[39][1] - x[38][1] * x[39][0];
        }
    } else {
        for (i = 1; i < 38; i++) {
            real_sum += x[i][0] * x[i][0] + x[i][1] * x[i][1];
        }
        phi[2][1][0] = real_sum + x[ 0][0] * x[ 0][0] + x[ 0][1] * x[ 0][1];
        phi[1][0][0] = real_sum + x[38][0] * x[38][0] + x[38][1] * x[38][1];
    }
}

static void sbr_autocorrelate_c(const float x[40][2], float phi[3][2][2])
{
#if 0
    /* This code is slower because it multiplies memory accesses.
     * It is left for educational purposes and because it may offer
     * a better reference for writing arch-specific DSP functions. */
    autocorrelate(x, phi, 0);
    autocorrelate(x, phi, 1);
    autocorrelate(x, phi, 2);
#else
    float real_sum2 = x[0][0] * x[2][0] + x[0][1] * x[2][1];
    float imag_sum2 = x[0][0] * x[2][1] - x[0][1] * x[2][0];
    float real_sum1 = 0.0f, imag_sum1 = 0.0f, real_sum0 = 0.0f;
    int   i;
    for (i = 1; i < 38; i++) {
        real_sum0 += x[i][0] * x[i    ][0] + x[i][1] * x[i    ][1];
        real_sum1 += x[i][0] * x[i + 1][0] + x[i][1] * x[i + 1][1];
        imag_sum1 += x[i][0] * x[i + 1][1] - x[i][1] * x[i + 1][0];
        real_sum2 += x[i][0] * x[i + 2][0] + x[i][1] * x[i + 2][1];
        imag_sum2 += x[i][0] * x[i + 2][1] - x[i][1] * x[i + 2][0];
    }
    phi[2 - 2][1][0] = real_sum2;
    phi[2 - 2][1][1] = imag_sum2;
    phi[2    ][1][0] = real_sum0 + x[ 0][0] * x[ 0][0] + x[ 0][1] * x[ 0][1];
    phi[1    ][0][0] = real_sum0 + x[38][0] * x[38][0] + x[38][1] * x[38][1];
    phi[2 - 1][1][0] = real_sum1 + x[ 0][0] * x[ 1][0] + x[ 0][1] * x[ 1][1];
    phi[2 - 1][1][1] = imag_sum1 + x[ 0][0] * x[ 1][1] - x[ 0][1] * x[ 1][0];
    phi[0    ][0][0] = real_sum1 + x[38][0] * x[39][0] + x[38][1] * x[39][1];
    phi[0    ][0][1] = imag_sum1 + x[38][0] * x[39][1] - x[38][1] * x[39][0];
#endif
}

static void sbr_hf_gen_c(float (*X_high)[2], const float (*X_low)[2],
                         const float alpha0[2], const float alpha1[2],
                         float bw, int start, int end)
{
    float alpha[4];
    int i;

    alpha[0] = alpha1[0] * bw * bw;
    alpha[1] = alpha1[1] * bw * bw;
    alpha[2] = alpha0[0] * bw;
    alpha[3] = alpha0[1] * bw;

    for (i = start; i < end; i++) {
        X_high[i][0] =
            X_low[i - 2][0] * alpha[0] -
            X_low[i - 2][1] * alpha[1] +
            X_low[i - 1][0] * alpha[2] -
            X_low[i - 1][1] * alpha[3] +
            X_low[i][0];
        X_high[i][1] =
            X_low[i - 2][1] * alpha[0] +
            X_low[i - 2][0] * alpha[1] +
            X_low[i - 1][1] * alpha[2] +
            X_low[i - 1][0] * alpha[3] +
            X_low[i][1];
    }
}

static void sbr_hf_g_filt_c(float (*Y)[2], const float (*X_high)[40][2],
                            const float *g_filt, int m_max, intptr_t ixh)
{
    int m;

    for (m = 0; m < m_max; m++) {
        Y[m][0] = X_high[m][ixh][0] * g_filt[m];
        Y[m][1] = X_high[m][ixh][1] * g_filt[m];
    }
}

static av_always_inline void sbr_hf_apply_noise(float (*Y)[2],
                                                const float *s_m,
                                                const float *q_filt,
                                                int noise,
                                                float phi_sign0,
                                                float phi_sign1,
                                                int m_max)
{
    int m;

    for (m = 0; m < m_max; m++) {
        float y0 = Y[m][0];
        float y1 = Y[m][1];
        noise = (noise + 1) & 0x1ff;
        if (s_m[m]) {
            y0 += s_m[m] * phi_sign0;
            y1 += s_m[m] * phi_sign1;
        } else {
            y0 += q_filt[m] * ff_sbr_noise_table[noise][0];
            y1 += q_filt[m] * ff_sbr_noise_table[noise][1];
        }
        Y[m][0] = y0;
        Y[m][1] = y1;
        phi_sign1 = -phi_sign1;
    }
}

static void sbr_hf_apply_noise_0(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    sbr_hf_apply_noise(Y, s_m, q_filt, noise, 1.0, 0.0, m_max);
}

static void sbr_hf_apply_noise_1(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    float phi_sign = 1 - 2 * (kx & 1);
    sbr_hf_apply_noise(Y, s_m, q_filt, noise, 0.0, phi_sign, m_max);
}

static void sbr_hf_apply_noise_2(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    sbr_hf_apply_noise(Y, s_m, q_filt, noise, -1.0, 0.0, m_max);
}

static void sbr_hf_apply_noise_3(float (*Y)[2], const float *s_m,
                                 const float *q_filt, int noise,
                                 int kx, int m_max)
{
    float phi_sign = 1 - 2 * (kx & 1);
    sbr_hf_apply_noise(Y, s_m, q_filt, noise, 0.0, -phi_sign, m_max);
}

av_cold void ff_sbrdsp_init(SBRDSPContext *s)
{
    s->sum64x5 = sbr_sum64x5_c;
    s->sum_square = sbr_sum_square_c;
    s->neg_odd_64 = sbr_neg_odd_64_c;
    s->qmf_pre_shuffle = sbr_qmf_pre_shuffle_c;
    s->qmf_post_shuffle = sbr_qmf_post_shuffle_c;
    s->qmf_deint_neg = sbr_qmf_deint_neg_c;
    s->qmf_deint_bfly = sbr_qmf_deint_bfly_c;
    s->autocorrelate = sbr_autocorrelate_c;
    s->hf_gen = sbr_hf_gen_c;
    s->hf_g_filt = sbr_hf_g_filt_c;

    s->hf_apply_noise[0] = sbr_hf_apply_noise_0;
    s->hf_apply_noise[1] = sbr_hf_apply_noise_1;
    s->hf_apply_noise[2] = sbr_hf_apply_noise_2;
    s->hf_apply_noise[3] = sbr_hf_apply_noise_3;

    if (ARCH_ARM)
        ff_sbrdsp_init_arm(s);
    if (ARCH_X86)
        ff_sbrdsp_init_x86(s);
}
