/*
 * Copyright (c) 2012-2014 Christophe Gisquet <christophe.gisquet@gmail.com>
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

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/dcadsp.h"

void ff_int8x8_fmul_int32_sse(float *dst, const int8_t *src, int scale);
void ff_int8x8_fmul_int32_sse2(float *dst, const int8_t *src, int scale);
void ff_int8x8_fmul_int32_sse4(float *dst, const int8_t *src, int scale);

av_cold void ff_dcadsp_init_x86(DCADSPContext *s)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE(cpu_flags)) {
#if ARCH_X86_32
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse;
#endif
    }

    if (EXTERNAL_SSE2(cpu_flags)) {
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse2;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        s->int8x8_fmul_int32 = ff_int8x8_fmul_int32_sse4;
    }
}
