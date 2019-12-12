/*
 * Copyright (c) 2015 Shivraj Patil (Shivraj.Patil@imgtec.com)
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
#include "pixblockdsp_mips.h"

static void diff_pixels_msa(int16_t *block, const uint8_t *src1,
                            const uint8_t *src2, int32_t stride)
{
    v16u8 in10, in11, in12, in13, in14, in15, in16, in17;
    v16u8 in20, in21, in22, in23, in24, in25, in26, in27;
    v8i16 out0, out1, out2, out3, out4, out5, out6, out7;

    LD_UB8(src1, stride, in10, in11, in12, in13, in14, in15, in16, in17);
    LD_UB8(src2, stride, in20, in21, in22, in23, in24, in25, in26, in27);
    ILVR_B4_SH(in10, in20, in11, in21, in12, in22, in13, in23,
               out0, out1, out2, out3);
    ILVR_B4_SH(in14, in24, in15, in25, in16, in26, in17, in27,
               out4, out5, out6, out7);
    HSUB_UB4_SH(out0, out1, out2, out3, out0, out1, out2, out3);
    HSUB_UB4_SH(out4, out5, out6, out7, out4, out5, out6, out7);
    ST_SH8(out0, out1, out2, out3, out4, out5, out6, out7, block, 8);
}

static void copy_8bit_to_16bit_width8_msa(const uint8_t *src, int32_t src_stride,
                                          int16_t *dst, int32_t dst_stride,
                                          int32_t height)
{
    uint8_t *dst_ptr;
    int32_t cnt;
    v16u8 src0, src1, src2, src3;
    v16i8 zero = { 0 };

    dst_ptr = (uint8_t *) dst;

    for (cnt = (height >> 2); cnt--;) {
        LD_UB4(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        ILVR_B4_UB(zero, src0, zero, src1, zero, src2, zero, src3,
                   src0, src1, src2, src3);

        ST_UB4(src0, src1, src2, src3, dst_ptr, (dst_stride * 2));
        dst_ptr += (4 * 2 * dst_stride);
    }
}

static void copy_16multx8mult_msa(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  int32_t height, int32_t width)
{
    int32_t cnt, loop_cnt;
    const uint8_t *src_tmp;
    uint8_t *dst_tmp;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    for (cnt = (width >> 4); cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        for (loop_cnt = (height >> 3); loop_cnt--;) {
            LD_UB8(src_tmp, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src_tmp += (8 * src_stride);

            ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7,
                   dst_tmp, dst_stride);
            dst_tmp += (8 * dst_stride);
        }

        src += 16;
        dst += 16;
    }
}

static void copy_width16_msa(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt;
    v16u8 src0, src1, src2, src3, src4, src5, src6, src7;

    if (0 == height % 12) {
        for (cnt = (height / 12); cnt--;) {
            LD_UB8(src, src_stride,
                   src0, src1, src2, src3, src4, src5, src6, src7);
            src += (8 * src_stride);
            ST_UB8(src0, src1, src2, src3, src4, src5, src6, src7,
                   dst, dst_stride);
            dst += (8 * dst_stride);

            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);
            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    } else if (0 == height % 8) {
        copy_16multx8mult_msa(src, src_stride, dst, dst_stride, height, 16);
    } else if (0 == height % 4) {
        for (cnt = (height >> 2); cnt--;) {
            LD_UB4(src, src_stride, src0, src1, src2, src3);
            src += (4 * src_stride);

            ST_UB4(src0, src1, src2, src3, dst, dst_stride);
            dst += (4 * dst_stride);
        }
    }
}

void ff_get_pixels_16_msa(int16_t *av_restrict dest, const uint8_t *src,
                          ptrdiff_t stride)
{
    copy_width16_msa(src, stride, (uint8_t *) dest, 16, 8);
}

void ff_get_pixels_8_msa(int16_t *av_restrict dest, const uint8_t *src,
                         ptrdiff_t stride)
{
    copy_8bit_to_16bit_width8_msa(src, stride, dest, 8, 8);
}

void ff_diff_pixels_msa(int16_t *av_restrict block, const uint8_t *src1,
                        const uint8_t *src2, ptrdiff_t stride)
{
    diff_pixels_msa(block, src1, src2, stride);
}
