/*
 * ARM NEON optimised HEVC IDCT
 * Copyright (c) 2017 Alexandra Hájková
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
#include "libavutil/arm/cpu.h"

#include "libavcodec/hevcdsp.h"

void ff_hevc_idct_4x4_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_4x4_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_10_neon(int16_t *coeffs, int col_limit);

av_cold void ff_hevc_dsp_init_arm(HEVCDSPContext *c, int bit_depth)
{
    int cpu_flags = av_get_cpu_flags();

    if (have_neon(cpu_flags)) {
        if (bit_depth == 8) {
            c->idct[0] = ff_hevc_idct_4x4_8_neon;
            c->idct[1] = ff_hevc_idct_8x8_8_neon;
            c->idct[2] = ff_hevc_idct_16x16_8_neon;
        }
        if (bit_depth == 10) {
            c->idct[0] = ff_hevc_idct_4x4_10_neon;
            c->idct[1] = ff_hevc_idct_8x8_10_neon;
            c->idct[2] = ff_hevc_idct_16x16_10_neon;
        }
    }
}
