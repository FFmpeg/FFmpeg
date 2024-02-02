/*
 * Copyright (c) 2024 Institue of Software Chinese Academy of Sciences (ISCAS).
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
#include "libavutil/riscv/cpu.h"
#include "libavcodec/vp8dsp.h"

void ff_vp8_idct_dc_add_rvv(uint8_t *dst, int16_t block[16], ptrdiff_t stride);
void ff_vp8_idct_dc_add4y_rvv(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);
void ff_vp8_idct_dc_add4uv_rvv(uint8_t *dst, int16_t block[4][16], ptrdiff_t stride);

av_cold void ff_vp8dsp_init_riscv(VP8DSPContext *c)
{
#if HAVE_RVV
    int flags = av_get_cpu_flags();

    if (flags & AV_CPU_FLAG_RVV_I32 && ff_get_rv_vlenb() >= 16) {
        c->vp8_idct_dc_add = ff_vp8_idct_dc_add_rvv;
        c->vp8_idct_dc_add4y = ff_vp8_idct_dc_add4y_rvv;
        if (flags & AV_CPU_FLAG_RVB_ADDR) {
            c->vp8_idct_dc_add4uv = ff_vp8_idct_dc_add4uv_rvv;
        }
    }
#endif
}
