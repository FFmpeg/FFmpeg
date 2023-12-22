/*
 * Copyright (c) 2015 Paul B Mahol
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
#include "libavcodec/takdsp.h"
#include "libavutil/x86/cpu.h"
#include "config.h"

void ff_tak_decorrelate_ls_sse2(const int32_t *p1, int32_t *p2, int length);
void ff_tak_decorrelate_ls_avx2(const int32_t *p1, int32_t *p2, int length);
void ff_tak_decorrelate_sr_sse2(int32_t *p1, const int32_t *p2, int length);
void ff_tak_decorrelate_sr_avx2(int32_t *p1, const int32_t *p2, int length);
void ff_tak_decorrelate_sm_sse2(int32_t *p1, int32_t *p2, int length);
void ff_tak_decorrelate_sm_avx2(int32_t *p1, int32_t *p2, int length);
void ff_tak_decorrelate_sf_sse4(int32_t *p1, const int32_t *p2, int length, int dshift, int dfactor);
void ff_tak_decorrelate_sf_avx2(int32_t *p1, const int32_t *p2, int length, int dshift, int dfactor);

av_cold void ff_takdsp_init_x86(TAKDSPContext *c)
{
#if HAVE_X86ASM
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->decorrelate_ls = ff_tak_decorrelate_ls_sse2;
        c->decorrelate_sr = ff_tak_decorrelate_sr_sse2;
        c->decorrelate_sm = ff_tak_decorrelate_sm_sse2;
    }

    if (EXTERNAL_SSE4(cpu_flags)) {
        c->decorrelate_sf = ff_tak_decorrelate_sf_sse4;
    }

    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->decorrelate_ls = ff_tak_decorrelate_ls_avx2;
        c->decorrelate_sr = ff_tak_decorrelate_sr_avx2;
        c->decorrelate_sm = ff_tak_decorrelate_sm_avx2;
        c->decorrelate_sf = ff_tak_decorrelate_sf_avx2;
    }
#endif
}
