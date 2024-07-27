/*
 * Copyright (c) 2023 SiFive, Inc. All rights reserved.
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
#include "libavutil/riscv/cpu.h"
#include "libavcodec/h264chroma.h"
#include "config.h"

void h264_put_chroma_mc8_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);
void h264_avg_chroma_mc8_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);
void h264_put_chroma_mc4_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);
void h264_avg_chroma_mc4_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);
void h264_put_chroma_mc2_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);
void h264_avg_chroma_mc2_rvv(uint8_t *p_dst, const uint8_t *p_src, ptrdiff_t stride, int h, int x, int y);

av_cold void ff_h264chroma_init_riscv(H264ChromaContext *c, int bit_depth)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (bit_depth == 8 && (flags & AV_CPU_FLAG_RVV_I32) &&
        (flags & AV_CPU_FLAG_RVB) && ff_rv_vlen_least(128)) {
        c->put_h264_chroma_pixels_tab[0] = h264_put_chroma_mc8_rvv;
        c->avg_h264_chroma_pixels_tab[0] = h264_avg_chroma_mc8_rvv;
        c->put_h264_chroma_pixels_tab[1] = h264_put_chroma_mc4_rvv;
        c->avg_h264_chroma_pixels_tab[1] = h264_avg_chroma_mc4_rvv;
        c->put_h264_chroma_pixels_tab[2] = h264_put_chroma_mc2_rvv;
        c->avg_h264_chroma_pixels_tab[2] = h264_avg_chroma_mc2_rvv;
    }
#endif
}
