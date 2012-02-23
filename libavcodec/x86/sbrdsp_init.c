/*
 * AAC Spectral Band Replication decoding functions
 * Copyright (c) 2012 Christophe Gisquet <christophe.gisquet@gmail.com>
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
#include "libavutil/cpu.h"
#include "libavcodec/sbrdsp.h"

float ff_sbr_sum_square_sse(float (*x)[2], int n);
void ff_sbr_hf_g_filt_sse(float (*Y)[2], const float (*X_high)[40][2],
                          const float *g_filt, int m_max, intptr_t ixh);

void ff_sbrdsp_init_x86(SBRDSPContext *s)
{
    if (HAVE_YASM) {
        int mm_flags = av_get_cpu_flags();

        if (mm_flags & AV_CPU_FLAG_SSE) {
            s->sum_square = ff_sbr_sum_square_sse;
            s->hf_g_filt = ff_sbr_hf_g_filt_sse;
        }
    }
}
