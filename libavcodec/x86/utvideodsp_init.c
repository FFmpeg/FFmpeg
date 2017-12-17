/*
 * Copyright (c) 2017 Paul B Mahol
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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/utvideodsp.h"

void ff_restore_rgb_planes_sse2(uint8_t *src_r, uint8_t *src_g, uint8_t *src_b,
                                ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                                ptrdiff_t linesize_b, int width, int height);
void ff_restore_rgb_planes_avx2(uint8_t *src_r, uint8_t *src_g, uint8_t *src_b,
                                ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                                ptrdiff_t linesize_b, int width, int height);

void ff_restore_rgb_planes10_sse2(uint16_t *src_r, uint16_t *src_g, uint16_t *src_b,
                                  ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                                  ptrdiff_t linesize_b, int width, int height);
void ff_restore_rgb_planes10_avx2(uint16_t *src_r, uint16_t *src_g, uint16_t *src_b,
                                  ptrdiff_t linesize_r, ptrdiff_t linesize_g,
                                  ptrdiff_t linesize_b, int width, int height);

av_cold void ff_utvideodsp_init_x86(UTVideoDSPContext *c)
{
    int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_SSE2(cpu_flags)) {
        c->restore_rgb_planes   = ff_restore_rgb_planes_sse2;
        c->restore_rgb_planes10 = ff_restore_rgb_planes10_sse2;
    }
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        c->restore_rgb_planes   = ff_restore_rgb_planes_avx2;
        c->restore_rgb_planes10 = ff_restore_rgb_planes10_avx2;
    }
}
