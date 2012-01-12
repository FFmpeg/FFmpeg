/*
 * Copyright (c) 2012 Mans Rullgard
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

#ifndef LIBAVCODEC_SBRDSP_H
#define LIBAVCODEC_SBRDSP_H

typedef struct SBRDSPContext {
    void (*sum64x5)(float *z);
    float (*sum_square)(float (*x)[2], int n);
    void (*neg_odd_64)(float *x);
    void (*qmf_pre_shuffle)(float *z);
    void (*qmf_post_shuffle)(float W[32][2], const float *z);
    void (*qmf_deint_neg)(float *v, const float *src);
    void (*qmf_deint_bfly)(float *v, const float *src0, const float *src1);
    void (*autocorrelate)(const float x[40][2], float phi[3][2][2]);
    void (*hf_gen)(float (*X_high)[2], const float (*X_low)[2],
                   const float alpha0[2], const float alpha1[2],
                   float bw, int start, int end);
    void (*hf_g_filt)(float (*Y)[2], const float (*X_high)[40][2],
                      const float *g_filt, int m_max, int ixh);
    void (*hf_apply_noise[4])(float (*Y)[2], const float *s_m,
                              const float *q_filt, int noise,
                              int kx, int m_max);
} SBRDSPContext;

extern const float ff_sbr_noise_table[][2];

void ff_sbrdsp_init(SBRDSPContext *s);
void ff_sbrdsp_init_arm(SBRDSPContext *s);

#endif
