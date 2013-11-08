/*
 * AAC Spectral Band Replication decoding functions
 * Copyright (c) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/sbrdsp.h"

float ff_sbr_sum_square_sse(float (*x)[2], int n);
void ff_sbr_sum64x5_sse(float *z);
void ff_sbr_hf_g_filt_sse(float (*Y)[2], const float (*X_high)[40][2],
                          const float *g_filt, int m_max, intptr_t ixh);
void ff_sbr_hf_gen_sse(float (*X_high)[2], const float (*X_low)[2],
                       const float alpha0[2], const float alpha1[2],
                       float bw, int start, int end);
void ff_sbr_neg_odd_64_sse(float *z);
void ff_sbr_qmf_post_shuffle_sse(float W[32][2], const float *z);
void ff_sbr_qmf_deint_bfly_sse(float *v, const float *src0, const float *src1);
void ff_sbr_qmf_deint_bfly_sse2(float *v, const float *src0, const float *src1);
void ff_sbr_qmf_pre_shuffle_sse2(float *z);

void ff_sbr_hf_apply_noise_0_sse2(float (*Y)[2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_1_sse2(float (*Y)[2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_2_sse2(float (*Y)[2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);
void ff_sbr_hf_apply_noise_3_sse2(float (*Y)[2], const float *s_m,
                                  const float *q_filt, int noise,
                                  int kx, int m_max);

av_cold void ff_sbrdsp_init_x86(SBRDSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
        s->neg_odd_64 = ff_sbr_neg_odd_64_sse;
        s->sum_square = ff_sbr_sum_square_sse;
        s->sum64x5    = ff_sbr_sum64x5_sse;
        s->hf_g_filt  = ff_sbr_hf_g_filt_sse;
        s->hf_gen     = ff_sbr_hf_gen_sse;
        s->qmf_post_shuffle = ff_sbr_qmf_post_shuffle_sse;
        s->qmf_deint_bfly   = ff_sbr_qmf_deint_bfly_sse;
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        s->qmf_deint_bfly   = ff_sbr_qmf_deint_bfly_sse2;
        s->qmf_pre_shuffle  = ff_sbr_qmf_pre_shuffle_sse2;
        s->hf_apply_noise[0] = ff_sbr_hf_apply_noise_0_sse2;
        s->hf_apply_noise[1] = ff_sbr_hf_apply_noise_1_sse2;
        s->hf_apply_noise[2] = ff_sbr_hf_apply_noise_2_sse2;
        s->hf_apply_noise[3] = ff_sbr_hf_apply_noise_3_sse2;
    }
}
