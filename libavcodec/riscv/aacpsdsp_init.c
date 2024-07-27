/*
 * Copyright © 2022 Rémi Denis-Courmont.
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
#include "libavcodec/aacpsdsp.h"

void ff_ps_add_squares_rvv(float *dst, const float (*src)[2], int n);
void ff_ps_mul_pair_single_rvv(float (*dst)[2], float (*src0)[2], float *src1,
                               int n);
void ff_ps_hybrid_analysis_rvv(float (*out)[2], float (*in)[2],
                               const float (*filter)[8][2], ptrdiff_t, int n);
void ff_ps_hybrid_analysis_ileave_rvv(float (*out)[32][2], float L[2][38][64],
                                      int i, int len);
void ff_ps_hybrid_synthesis_deint_rvv(float out[2][38][64], float (*in)[32][2],
                                      int i, int len);

void ff_ps_stereo_interpolate_rvv(float (*l)[2], float (*r)[2],
                                  float h[2][4], float h_step[2][4], int len);

av_cold void ff_psdsp_init_riscv(PSDSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32) {
        if (flags & AV_CPU_FLAG_RVV_F32) {
            if (flags & AV_CPU_FLAG_RVB) {
                if (flags & AV_CPU_FLAG_RVV_I64)
                    c->add_squares = ff_ps_add_squares_rvv;

                c->mul_pair_single = ff_ps_mul_pair_single_rvv;
            }
            c->hybrid_analysis = ff_ps_hybrid_analysis_rvv;
        }

        if (flags & AV_CPU_FLAG_RVB) {
            c->hybrid_analysis_ileave = ff_ps_hybrid_analysis_ileave_rvv;

            if (flags & AV_CPU_FLAG_RVV_I64)
                c->hybrid_synthesis_deint = ff_ps_hybrid_synthesis_deint_rvv;
            if (flags & AV_CPU_FLAG_RVV_F32)
                c->stereo_interpolate[0] = ff_ps_stereo_interpolate_rvv;
        }
    }
#endif
}
