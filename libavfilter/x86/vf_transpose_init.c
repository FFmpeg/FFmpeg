/*
 * Copyright (C) 2019 Paul B Mahol
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavfilter/transpose.h"

void ff_transpose_8x8_8_sse2(uint8_t *src,
                             ptrdiff_t src_linesize,
                             uint8_t *dst,
                             ptrdiff_t dst_linesize);

void ff_transpose_8x8_16_sse2(uint8_t *src,
                              ptrdiff_t src_linesize,
                              uint8_t *dst,
                              ptrdiff_t dst_linesize);

av_cold void ff_transpose_init_x86(TransVtable *v, int pixstep)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags) && pixstep == 1) {
        v->transpose_8x8 = ff_transpose_8x8_8_sse2;
    }

    if (EXTERNAL_SSE2(cpu_flags) && pixstep == 2) {
        v->transpose_8x8 = ff_transpose_8x8_16_sse2;
    }
}
