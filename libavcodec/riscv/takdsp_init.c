/*
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/riscv/cpu.h"
#include "libavcodec/takdsp.h"

void ff_decorrelate_ls_rvv(const int32_t *p1, int32_t *p2, int length);
void ff_decorrelate_sr_rvv(int32_t *p1, const int32_t *p2, int length);
void ff_decorrelate_sm_rvv(int32_t *p1, int32_t *p2, int length);
void ff_decorrelate_sf_rvv(int32_t *p1, const int32_t *p2, int len, int, int);

av_cold void ff_takdsp_init_riscv(TAKDSPContext *dsp)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if ((flags & AV_CPU_FLAG_RVV_I32) && (flags & AV_CPU_FLAG_RVB)) {
        dsp->decorrelate_ls = ff_decorrelate_ls_rvv;
        dsp->decorrelate_sr = ff_decorrelate_sr_rvv;
        dsp->decorrelate_sm = ff_decorrelate_sm_rvv;
        dsp->decorrelate_sf = ff_decorrelate_sf_rvv;
    }
#endif
}
