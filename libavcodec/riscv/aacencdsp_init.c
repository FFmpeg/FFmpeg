/*
 * AAC encoder assembly optimizations
 * Copyright (c) 2023 Institue of Software Chinese Academy of Sciences (ISCAS).
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
#include "libavcodec/aacencdsp.h"

void ff_abs_pow34_rvv(float *out, const float *in, const int size);
void ff_aac_quant_bands_rvv(int *, const float *, const float *, int, int,
                            int, const float, const float);

av_cold void ff_aacenc_dsp_init_riscv(AACEncDSPContext *s)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_F32) {
        if (flags & AV_CPU_FLAG_RVB) {
            s->abs_pow34 = ff_abs_pow34_rvv;
            s->quant_bands = ff_aac_quant_bands_rvv;
        }
    }
#endif
}
