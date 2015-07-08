/*
 * Copyright (c) 2015 Manojkumar Bhosale (Manojkumar.Bhosale@imgtec.com)
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

#include "libavutil/mips/generic_macros_msa.h"
#include "idctdsp_mips.h"

static void put_pixels_clamped_msa(const int16_t *block, uint8_t *pixels,
                                   int32_t stride)
{
    uint64_t in0_d, in1_d, in2_d, in3_d, in4_d, in5_d, in6_d, in7_d;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    CLIP_SH4_0_255(in0, in1, in2, in3);
    CLIP_SH4_0_255(in4, in5, in6, in7);
    PCKEV_B4_SH(in0, in0, in1, in1, in2, in2, in3, in3, in0, in1, in2, in3);
    PCKEV_B4_SH(in4, in4, in5, in5, in6, in6, in7, in7, in4, in5, in6, in7);

    in0_d = __msa_copy_u_d((v2i64) in0, 0);
    in1_d = __msa_copy_u_d((v2i64) in1, 0);
    in2_d = __msa_copy_u_d((v2i64) in2, 0);
    in3_d = __msa_copy_u_d((v2i64) in3, 0);
    in4_d = __msa_copy_u_d((v2i64) in4, 0);
    in5_d = __msa_copy_u_d((v2i64) in5, 0);
    in6_d = __msa_copy_u_d((v2i64) in6, 0);
    in7_d = __msa_copy_u_d((v2i64) in7, 0);
    SD4(in0_d, in1_d, in2_d, in3_d, pixels, stride);
    pixels += 4 * stride;
    SD4(in4_d, in5_d, in6_d, in7_d, pixels, stride);
}

static void put_signed_pixels_clamped_msa(const int16_t *block, uint8_t *pixels,
                                          int32_t stride)
{
    uint64_t in0_d, in1_d, in2_d, in3_d, in4_d, in5_d, in6_d, in7_d;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);

    in0 += 128;
    in1 += 128;
    in2 += 128;
    in3 += 128;
    in4 += 128;
    in5 += 128;
    in6 += 128;
    in7 += 128;

    CLIP_SH4_0_255(in0, in1, in2, in3);
    CLIP_SH4_0_255(in4, in5, in6, in7);
    PCKEV_B4_SH(in0, in0, in1, in1, in2, in2, in3, in3, in0, in1, in2, in3);
    PCKEV_B4_SH(in4, in4, in5, in5, in6, in6, in7, in7, in4, in5, in6, in7);

    in0_d = __msa_copy_u_d((v2i64) in0, 0);
    in1_d = __msa_copy_u_d((v2i64) in1, 0);
    in2_d = __msa_copy_u_d((v2i64) in2, 0);
    in3_d = __msa_copy_u_d((v2i64) in3, 0);
    in4_d = __msa_copy_u_d((v2i64) in4, 0);
    in5_d = __msa_copy_u_d((v2i64) in5, 0);
    in6_d = __msa_copy_u_d((v2i64) in6, 0);
    in7_d = __msa_copy_u_d((v2i64) in7, 0);
    SD4(in0_d, in1_d, in2_d, in3_d, pixels, stride);
    pixels += 4 * stride;
    SD4(in4_d, in5_d, in6_d, in7_d, pixels, stride);
}

static void add_pixels_clamped_msa(const int16_t *block, uint8_t *pixels,
                                   int32_t stride)
{
    uint64_t in0_d, in1_d, in2_d, in3_d, in4_d, in5_d, in6_d, in7_d;
    v8i16 in0, in1, in2, in3, in4, in5, in6, in7;
    v16u8 pix_in0, pix_in1, pix_in2, pix_in3;
    v16u8 pix_in4, pix_in5, pix_in6, pix_in7;
    v8u16 pix0, pix1, pix2, pix3, pix4, pix5, pix6, pix7;
    v8i16 zero = { 0 };

    LD_SH8(block, 8, in0, in1, in2, in3, in4, in5, in6, in7);
    LD_UB8(pixels, stride, pix_in0, pix_in1, pix_in2,
           pix_in3, pix_in4, pix_in5, pix_in6, pix_in7);

    ILVR_B4_UH(zero, pix_in0, zero, pix_in1, zero, pix_in2, zero, pix_in3,
               pix0, pix1, pix2, pix3);
    ILVR_B4_UH(zero, pix_in4, zero, pix_in5, zero, pix_in6, zero, pix_in7,
               pix4, pix5, pix6, pix7);

    in0 += (v8i16) pix0;
    in1 += (v8i16) pix1;
    in2 += (v8i16) pix2;
    in3 += (v8i16) pix3;
    in4 += (v8i16) pix4;
    in5 += (v8i16) pix5;
    in6 += (v8i16) pix6;
    in7 += (v8i16) pix7;

    CLIP_SH4_0_255(in0, in1, in2, in3);
    CLIP_SH4_0_255(in4, in5, in6, in7);
    PCKEV_B4_SH(in0, in0, in1, in1, in2, in2, in3, in3, in0, in1, in2, in3);
    PCKEV_B4_SH(in4, in4, in5, in5, in6, in6, in7, in7, in4, in5, in6, in7);

    in0_d = __msa_copy_u_d((v2i64) in0, 0);
    in1_d = __msa_copy_u_d((v2i64) in1, 0);
    in2_d = __msa_copy_u_d((v2i64) in2, 0);
    in3_d = __msa_copy_u_d((v2i64) in3, 0);
    in4_d = __msa_copy_u_d((v2i64) in4, 0);
    in5_d = __msa_copy_u_d((v2i64) in5, 0);
    in6_d = __msa_copy_u_d((v2i64) in6, 0);
    in7_d = __msa_copy_u_d((v2i64) in7, 0);
    SD4(in0_d, in1_d, in2_d, in3_d, pixels, stride);
    pixels += 4 * stride;
    SD4(in4_d, in5_d, in6_d, in7_d, pixels, stride);
}

void ff_put_pixels_clamped_msa(const int16_t *block,
                               uint8_t *av_restrict pixels,
                               ptrdiff_t line_size)
{
    put_pixels_clamped_msa(block, pixels, line_size);
}

void ff_put_signed_pixels_clamped_msa(const int16_t *block,
                                      uint8_t *av_restrict pixels,
                                      ptrdiff_t line_size)
{
    put_signed_pixels_clamped_msa(block, pixels, line_size);
}

void ff_add_pixels_clamped_msa(const int16_t *block,
                               uint8_t *av_restrict pixels,
                               ptrdiff_t line_size)
{
    add_pixels_clamped_msa(block, pixels, line_size);
}
