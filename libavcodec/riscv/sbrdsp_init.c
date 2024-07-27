/*
 * Copyright © 2023 Rémi Denis-Courmont.
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
#include "libavutil/riscv/cpu.h"
#include "libavcodec/sbrdsp.h"

void ff_sbr_sum64x5_rvv(float *z);
float ff_sbr_sum_square_rvv(float (*x)[2], int n);
void ff_sbr_autocorrelate_rvv(const float x[40][2], float phi[3][2][2]);
void ff_sbr_hf_gen_rvv(float (*X_high)[2], const float (*X_low)[2],
                       const float alpha0[2], const float alpha1[2],
                       float bw, int start, int end);
void ff_sbr_hf_g_filt_rvv(float (*Y)[2], const float (*X_high)[40][2],
                          const float *g_filt, int m_max, intptr_t ixh);
void ff_sbr_hf_apply_noise_0_rvv(float (*Y)[2], const float *s,
                                 const float *f, int n, int kx, int max);
void ff_sbr_hf_apply_noise_1_rvv(float (*Y)[2], const float *s,
                                 const float *f, int n, int kx, int max);
void ff_sbr_hf_apply_noise_2_rvv(float (*Y)[2], const float *s,
                                 const float *f, int n, int kx, int max);
void ff_sbr_hf_apply_noise_3_rvv(float (*Y)[2], const float *s,
                                 const float *f, int n, int kx, int max);

av_cold void ff_sbrdsp_init_riscv(SBRDSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_F32) {
        if (flags & AV_CPU_FLAG_RVB) {
            c->sum64x5 = ff_sbr_sum64x5_rvv;
            c->sum_square = ff_sbr_sum_square_rvv;
            c->hf_gen = ff_sbr_hf_gen_rvv;
            c->hf_g_filt = ff_sbr_hf_g_filt_rvv;
            if (ff_get_rv_vlenb() <= 32) {
                c->hf_apply_noise[0] = ff_sbr_hf_apply_noise_0_rvv;
                c->hf_apply_noise[2] = ff_sbr_hf_apply_noise_2_rvv;
                c->hf_apply_noise[1] = ff_sbr_hf_apply_noise_1_rvv;
                c->hf_apply_noise[3] = ff_sbr_hf_apply_noise_3_rvv;
            }
        }
        c->autocorrelate = ff_sbr_autocorrelate_rvv;
    }
#endif
}
