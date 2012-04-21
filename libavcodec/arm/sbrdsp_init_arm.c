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

#include "config.h"
#include "libavutil/arm/cpu.h"
#include "libavutil/attributes.h"
#include "libavcodec/sbrdsp.h"

void ff_sbr_sum64x5_neon(float *z);
float ff_sbr_sum_square_neon(float (*x)[2], int n);
void ff_sbr_neg_odd_64_neon(float *x);
void ff_sbr_qmf_pre_shuffle_neon(float *z);
void ff_sbr_qmf_post_shuffle_neon(float W[32][2], const float *z);
void ff_sbr_qmf_deint_neg_neon(float *v, const float *src);
void ff_sbr_qmf_deint_bfly_neon(float *v, const float *src0, const float *src1);
void ff_sbr_hf_g_filt_neon(float (*Y)[2], const float (*X_high)[40][2],
                           const float *g_filt, int m_max, intptr_t ixh);
void ff_sbr_hf_gen_neon(float (*X_high)[2], const float (*X_low)[2],
                        const float alpha0[2], const float alpha1[2],
                        float bw, int start, int end);
void ff_sbr_autocorrelate_neon(const float x[40][2], float phi[3][2][2]);

void ff_sbr_hf_apply_noise_0_neon(float Y[64][2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_1_neon(float Y[64][2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_2_neon(float Y[64][2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_3_neon(float Y[64][2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);

av_cold void ff_sbrdsp_init_arm(SBRDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        s->sum64x5 = ff_sbr_sum64x5_neon;
        s->sum_square = ff_sbr_sum_square_neon;
        s->neg_odd_64 = ff_sbr_neg_odd_64_neon;
        s->qmf_pre_shuffle = ff_sbr_qmf_pre_shuffle_neon;
        s->qmf_post_shuffle = ff_sbr_qmf_post_shuffle_neon;
        s->qmf_deint_neg = ff_sbr_qmf_deint_neg_neon;
        s->qmf_deint_bfly = ff_sbr_qmf_deint_bfly_neon;
        s->hf_g_filt = ff_sbr_hf_g_filt_neon;
        s->hf_gen = ff_sbr_hf_gen_neon;
        s->autocorrelate = ff_sbr_autocorrelate_neon;
        s->hf_apply_noise[0] = ff_sbr_hf_apply_noise_0_neon;
        s->hf_apply_noise[1] = ff_sbr_hf_apply_noise_1_neon;
        s->hf_apply_noise[2] = ff_sbr_hf_apply_noise_2_neon;
        s->hf_apply_noise[3] = ff_sbr_hf_apply_noise_3_neon;
    }
}
