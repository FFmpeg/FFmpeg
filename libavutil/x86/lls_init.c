/*
 * linear least squares model
 *
 * Copyright (c) 2013 Loren Merritt
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

#include "libavutil/lls.h"
#include "libavutil/x86/cpu.h"

void ff_update_lls_sse2(LLSModel *m, double *var);
void ff_update_lls_avx(LLSModel *m, double *var);
double ff_evaluate_lls_sse2(LLSModel *m, double *var, int order);

av_cold void ff_init_lls_x86(LLSModel *m)
{
    int cpu_flags = av_get_cpu_flags();
    if (EXTERNAL_SSE2(cpu_flags)) {
        m->update_lls = ff_update_lls_sse2;
        if (m->indep_count >= 4)
            m->evaluate_lls = ff_evaluate_lls_sse2;
    }
    if (EXTERNAL_AVX(cpu_flags)) {
        m->update_lls = ff_update_lls_avx;
    }
}
