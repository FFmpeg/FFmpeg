/*
 * Copyright (c) 2020 Reimar Döffinger
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
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/hevcdsp.h"

void ff_hevc_idct_8x8_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_8x8_10_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_8_neon(int16_t *coeffs, int col_limit);
void ff_hevc_idct_16x16_10_neon(int16_t *coeffs, int col_limit);

av_cold void ff_hevc_dsp_init_aarch64(HEVCDSPContext *c, const int bit_depth)
{
    if (!have_neon(av_get_cpu_flags())) return;

    if (bit_depth == 8) {
        c->idct[1]                     = ff_hevc_idct_8x8_8_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_8_neon;
    }
    if (bit_depth == 10) {
        c->idct[1]                     = ff_hevc_idct_8x8_10_neon;
        c->idct[2]                     = ff_hevc_idct_16x16_10_neon;
    }
}
