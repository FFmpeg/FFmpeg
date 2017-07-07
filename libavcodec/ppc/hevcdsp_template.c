/*
 * Copyright (c) Alexandra Hajkova
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

static void FUNC(ff_hevc_idct_4x4, BIT_DEPTH)(int16_t *coeffs, int col_limit)
{
    const int shift = 7;
    const int shift2 = 20 - BIT_DEPTH;
    vec_s16 src_01, src_23;
    vec_s32 res[4];
    vec_s16 res_packed[2];

    src_01 = vec_ld(0, coeffs);
    src_23 = vec_ld(16, coeffs);

    transform4x4(src_01, src_23, res, shift, coeffs);
    src_01 = vec_packs(res[0], res[1]);
    src_23 = vec_packs(res[2], res[3]);
    scale(res, res_packed, shift);
    // transpose
    src_01 = vec_perm(res_packed[0], res_packed[1], mask[0]);
    src_23 = vec_perm(res_packed[0], res_packed[1], mask[1]);

    transform4x4(src_01, src_23, res, shift2, coeffs);
    scale(res, res_packed, shift2);
    // transpose
    src_01 = vec_perm(res_packed[0], res_packed[1], mask[0]);
    src_23 = vec_perm(res_packed[0], res_packed[1], mask[1]);

    vec_st(src_01, 0, coeffs);
    vec_st(src_23, 16, coeffs);
}
