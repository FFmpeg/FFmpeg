/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Shiyou Yin <yinshiyou-hf@loongson.cn>
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

#include "libavutil/loongarch/loongson_intrinsics.h"
#include "hpeldsp_lasx.h"

static av_always_inline void
put_pixels8_l2_8_lsx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                     int dst_stride, int src_stride1, int src_stride2, int h)
{
    int stride1_2, stride1_3, stride1_4;
    int stride2_2, stride2_3, stride2_4;
    __asm__ volatile (
        "slli.d   %[stride1_2],  %[srcStride1],     1             \n\t"
        "slli.d   %[stride2_2],  %[srcStride2],     1             \n\t"
        "add.d    %[stride1_3],  %[stride1_2],      %[srcStride1] \n\t"
        "add.d    %[stride2_3],  %[stride2_2],      %[srcStride2] \n\t"
        "slli.d   %[stride1_4],  %[stride1_2],      1             \n\t"
        "slli.d   %[stride2_4],  %[stride2_2],      1             \n\t"
        "1:                                                       \n\t"
        "vld      $vr0,          %[src1],           0             \n\t"
        "vldx     $vr1,          %[src1],           %[srcStride1] \n\t"
        "vldx     $vr2,          %[src1],           %[stride1_2]  \n\t"
        "vldx     $vr3,          %[src1],           %[stride1_3]  \n\t"
        "add.d    %[src1],       %[src1],           %[stride1_4]  \n\t"

        "vld      $vr4,          %[src2],           0             \n\t"
        "vldx     $vr5,          %[src2],           %[srcStride2] \n\t"
        "vldx     $vr6,          %[src2],           %[stride2_2]  \n\t"
        "vldx     $vr7,          %[src2],           %[stride2_3]  \n\t"
        "add.d    %[src2],       %[src2],           %[stride2_4]  \n\t"

        "addi.d   %[h],          %[h],              -4            \n\t"

        "vavgr.bu $vr0,          $vr4,              $vr0          \n\t"
        "vavgr.bu $vr1,          $vr5,              $vr1          \n\t"
        "vavgr.bu $vr2,          $vr6,              $vr2          \n\t"
        "vavgr.bu $vr3,          $vr7,              $vr3          \n\t"
        "vstelm.d $vr0,          %[dst],            0,  0         \n\t"
        "add.d    %[dst],        %[dst],            %[dstStride]  \n\t"
        "vstelm.d $vr1,          %[dst],            0,  0         \n\t"
        "add.d    %[dst],        %[dst],            %[dstStride]  \n\t"
        "vstelm.d $vr2,          %[dst],            0,  0         \n\t"
        "add.d    %[dst],        %[dst],            %[dstStride]  \n\t"
        "vstelm.d $vr3,          %[dst],            0,  0         \n\t"
        "add.d    %[dst],        %[dst],            %[dstStride]  \n\t"
        "bnez     %[h],                             1b            \n\t"

        : [dst]"+&r"(dst), [src2]"+&r"(src2), [src1]"+&r"(src1),
          [h]"+&r"(h), [stride1_2]"=&r"(stride1_2),
          [stride1_3]"=&r"(stride1_3), [stride1_4]"=&r"(stride1_4),
          [stride2_2]"=&r"(stride2_2), [stride2_3]"=&r"(stride2_3),
          [stride2_4]"=&r"(stride2_4)
        : [dstStride]"r"(dst_stride), [srcStride1]"r"(src_stride1),
          [srcStride2]"r"(src_stride2)
        : "memory"
    );
}

static av_always_inline void
put_pixels16_l2_8_lsx(uint8_t *dst, const uint8_t *src1, const uint8_t *src2,
                      int dst_stride, int src_stride1, int src_stride2, int h)
{
    int stride1_2, stride1_3, stride1_4;
    int stride2_2, stride2_3, stride2_4;
    int dststride2, dststride3, dststride4;
    __asm__ volatile (
        "slli.d   %[stride1_2],  %[srcStride1],     1             \n\t"
        "slli.d   %[stride2_2],  %[srcStride2],     1             \n\t"
        "slli.d   %[dststride2], %[dstStride],      1             \n\t"
        "add.d    %[stride1_3],  %[stride1_2],      %[srcStride1] \n\t"
        "add.d    %[stride2_3],  %[stride2_2],      %[srcStride2] \n\t"
        "add.d    %[dststride3], %[dststride2],     %[dstStride]  \n\t"
        "slli.d   %[stride1_4],  %[stride1_2],      1             \n\t"
        "slli.d   %[stride2_4],  %[stride2_2],      1             \n\t"
        "slli.d   %[dststride4], %[dststride2],     1             \n\t"
        "1:                                                       \n\t"
        "vld      $vr0,          %[src1],           0             \n\t"
        "vldx     $vr1,          %[src1],           %[srcStride1] \n\t"
        "vldx     $vr2,          %[src1],           %[stride1_2]  \n\t"
        "vldx     $vr3,          %[src1],           %[stride1_3]  \n\t"
        "add.d    %[src1],       %[src1],           %[stride1_4]  \n\t"

        "vld      $vr4,          %[src2],           0             \n\t"
        "vldx     $vr5,          %[src2],           %[srcStride2] \n\t"
        "vldx     $vr6,          %[src2],           %[stride2_2]  \n\t"
        "vldx     $vr7,          %[src2],           %[stride2_3]  \n\t"
        "add.d    %[src2],       %[src2],           %[stride2_4]  \n\t"

        "addi.d   %[h],          %[h],              -4            \n\t"

        "vavgr.bu $vr0,          $vr4,              $vr0          \n\t"
        "vavgr.bu $vr1,          $vr5,              $vr1          \n\t"
        "vavgr.bu $vr2,          $vr6,              $vr2          \n\t"
        "vavgr.bu $vr3,          $vr7,              $vr3          \n\t"
        "vst      $vr0,          %[dst],            0             \n\t"
        "vstx     $vr1,          %[dst],            %[dstStride]  \n\t"
        "vstx     $vr2,          %[dst],            %[dststride2] \n\t"
        "vstx     $vr3,          %[dst],            %[dststride3] \n\t"
        "add.d    %[dst],        %[dst],            %[dststride4] \n\t"
        "bnez     %[h],                             1b            \n\t"

        : [dst]"+&r"(dst), [src2]"+&r"(src2), [src1]"+&r"(src1),
          [h]"+&r"(h), [stride1_2]"=&r"(stride1_2),
          [stride1_3]"=&r"(stride1_3), [stride1_4]"=&r"(stride1_4),
          [stride2_2]"=&r"(stride2_2), [stride2_3]"=&r"(stride2_3),
          [stride2_4]"=&r"(stride2_4), [dststride2]"=&r"(dststride2),
          [dststride3]"=&r"(dststride3), [dststride4]"=&r"(dststride4)
        : [dstStride]"r"(dst_stride), [srcStride1]"r"(src_stride1),
          [srcStride2]"r"(src_stride2)
        : "memory"
    );
}

void ff_put_pixels8_8_lasx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    uint64_t tmp[8];
    int h_8 = h >> 3;
    int res = h & 7;
    ptrdiff_t stride2, stride3, stride4;

    __asm__ volatile (
        "beqz     %[h_8],                           2f            \n\t"
        "slli.d   %[stride2],    %[stride],         1             \n\t"
        "add.d    %[stride3],    %[stride2],        %[stride]     \n\t"
        "slli.d   %[stride4],    %[stride2],        1             \n\t"
        "1:                                                       \n\t"
        "ld.d     %[tmp0],       %[src],            0x0           \n\t"
        "ldx.d    %[tmp1],       %[src],            %[stride]     \n\t"
        "ldx.d    %[tmp2],       %[src],            %[stride2]    \n\t"
        "ldx.d    %[tmp3],       %[src],            %[stride3]    \n\t"
        "add.d    %[src],        %[src],            %[stride4]    \n\t"
        "ld.d     %[tmp4],       %[src],            0x0           \n\t"
        "ldx.d    %[tmp5],       %[src],            %[stride]     \n\t"
        "ldx.d    %[tmp6],       %[src],            %[stride2]    \n\t"
        "ldx.d    %[tmp7],       %[src],            %[stride3]    \n\t"
        "add.d    %[src],        %[src],            %[stride4]    \n\t"

        "addi.d   %[h_8],        %[h_8],            -1            \n\t"

        "st.d     %[tmp0],       %[dst],            0x0           \n\t"
        "stx.d    %[tmp1],       %[dst],            %[stride]     \n\t"
        "stx.d    %[tmp2],       %[dst],            %[stride2]    \n\t"
        "stx.d    %[tmp3],       %[dst],            %[stride3]    \n\t"
        "add.d    %[dst],        %[dst],            %[stride4]    \n\t"
        "st.d     %[tmp4],       %[dst],            0x0           \n\t"
        "stx.d    %[tmp5],       %[dst],            %[stride]     \n\t"
        "stx.d    %[tmp6],       %[dst],            %[stride2]    \n\t"
        "stx.d    %[tmp7],       %[dst],            %[stride3]    \n\t"
        "add.d    %[dst],        %[dst],            %[stride4]    \n\t"
        "bnez     %[h_8],        1b                               \n\t"

        "2:                                                       \n\t"
        "beqz     %[res],        4f                               \n\t"
        "3:                                                       \n\t"
        "ld.d     %[tmp0],       %[src],            0x0           \n\t"
        "add.d    %[src],        %[src],            %[stride]     \n\t"
        "addi.d   %[res],        %[res],            -1            \n\t"
        "st.d     %[tmp0],       %[dst],            0x0           \n\t"
        "add.d    %[dst],        %[dst],            %[stride]     \n\t"
        "bnez     %[res],        3b                               \n\t"
        "4:                                                       \n\t"
        : [tmp0]"=&r"(tmp[0]),        [tmp1]"=&r"(tmp[1]),
          [tmp2]"=&r"(tmp[2]),        [tmp3]"=&r"(tmp[3]),
          [tmp4]"=&r"(tmp[4]),        [tmp5]"=&r"(tmp[5]),
          [tmp6]"=&r"(tmp[6]),        [tmp7]"=&r"(tmp[7]),
          [dst]"+&r"(block),          [src]"+&r"(pixels),
          [h_8]"+&r"(h_8),            [res]"+&r"(res),
          [stride2]"=&r"(stride2),    [stride3]"=&r"(stride3),
          [stride4]"=&r"(stride4)
        : [stride]"r"(line_size)
        : "memory"
    );
}

void ff_put_pixels16_8_lsx(uint8_t *block, const uint8_t *pixels,
                           ptrdiff_t line_size, int h)
{
    int h_8 = h >> 3;
    int res = h & 7;
    ptrdiff_t stride2, stride3, stride4;

    __asm__ volatile (
        "beqz     %[h_8],                           2f            \n\t"
        "slli.d   %[stride2],    %[stride],         1             \n\t"
        "add.d    %[stride3],    %[stride2],        %[stride]     \n\t"
        "slli.d   %[stride4],    %[stride2],        1             \n\t"
        "1:                                                       \n\t"
        "vld      $vr0,          %[src],            0x0           \n\t"
        "vldx     $vr1,          %[src],            %[stride]     \n\t"
        "vldx     $vr2,          %[src],            %[stride2]    \n\t"
        "vldx     $vr3,          %[src],            %[stride3]    \n\t"
        "add.d    %[src],        %[src],            %[stride4]    \n\t"
        "vld      $vr4,          %[src],            0x0           \n\t"
        "vldx     $vr5,          %[src],            %[stride]     \n\t"
        "vldx     $vr6,          %[src],            %[stride2]    \n\t"
        "vldx     $vr7,          %[src],            %[stride3]    \n\t"
        "add.d    %[src],        %[src],            %[stride4]    \n\t"

        "addi.d   %[h_8],        %[h_8],            -1            \n\t"

        "vst      $vr0,          %[dst],            0x0           \n\t"
        "vstx     $vr1,          %[dst],            %[stride]     \n\t"
        "vstx     $vr2,          %[dst],            %[stride2]    \n\t"
        "vstx     $vr3,          %[dst],            %[stride3]    \n\t"
        "add.d    %[dst],        %[dst],            %[stride4]    \n\t"
        "vst      $vr4,          %[dst],            0x0           \n\t"
        "vstx     $vr5,          %[dst],            %[stride]     \n\t"
        "vstx     $vr6,          %[dst],            %[stride2]    \n\t"
        "vstx     $vr7,          %[dst],            %[stride3]    \n\t"
        "add.d    %[dst],        %[dst],            %[stride4]    \n\t"
        "bnez     %[h_8],        1b                               \n\t"

        "2:                                                       \n\t"
        "beqz     %[res],        4f                               \n\t"
        "3:                                                       \n\t"
        "vld      $vr0,          %[src],            0x0           \n\t"
        "add.d    %[src],        %[src],            %[stride]     \n\t"
        "addi.d   %[res],        %[res],            -1            \n\t"
        "vst      $vr0,          %[dst],            0x0           \n\t"
        "add.d    %[dst],        %[dst],            %[stride]     \n\t"
        "bnez     %[res],        3b                               \n\t"
        "4:                                                       \n\t"
        : [dst]"+&r"(block),          [src]"+&r"(pixels),
          [h_8]"+&r"(h_8),            [res]"+&r"(res),
          [stride2]"=&r"(stride2),    [stride3]"=&r"(stride3),
          [stride4]"=&r"(stride4)
        : [stride]"r"(line_size)
        : "memory"
    );
}

void ff_put_pixels8_x2_8_lasx(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h)
{
    put_pixels8_l2_8_lsx(block, pixels, pixels + 1, line_size, line_size,
                         line_size, h);
}

void ff_put_pixels8_y2_8_lasx(uint8_t *block, const uint8_t *pixels,
                              ptrdiff_t line_size, int h)
{
    put_pixels8_l2_8_lsx(block, pixels, pixels + line_size, line_size,
                         line_size, line_size, h);
}

void ff_put_pixels16_x2_8_lasx(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h)
{
    put_pixels16_l2_8_lsx(block, pixels, pixels + 1, line_size, line_size,
                          line_size, h);
}

void ff_put_pixels16_y2_8_lasx(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h)
{
    put_pixels16_l2_8_lsx(block, pixels, pixels + line_size, line_size,
                          line_size, line_size, h);
}

static void common_hz_bil_no_rnd_16x16_lasx(const uint8_t *src,
                                            int32_t src_stride,
                                            uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t *_src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x -1);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5,
              src4, 0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
    dst += dst_stride;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
    dst += dst_stride;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
    dst += dst_stride;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
}

static void common_hz_bil_no_rnd_8x16_lasx(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
    dst += dst_stride;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src1, dst, 0, 2);
    __lasx_xvstelm_d(src1, dst, 8, 3);
}

void ff_put_no_rnd_pixels16_x2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                      ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_hz_bil_no_rnd_16x16_lasx(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_hz_bil_no_rnd_8x16_lasx(pixels, line_size, block, line_size);
    }
}

static void common_vt_bil_no_rnd_16x16_lasx(const uint8_t *src,
                                            int32_t src_stride,
                                            uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    __m256i src9, src10, src11, src12, src13, src14, src15, src16;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src8 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src9, src10);
    src11 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src12 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src13, src14);
    src15 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src16 = __lasx_xvld(_src, 0);

    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2,
              0x20, src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src5, src4, 0x20, src6, src5, 0x20, src7, src6,
              0x20, src8, src7, 0x20, src4, src5, src6, src7);
    DUP4_ARG3(__lasx_xvpermi_q, src9, src8, 0x20, src10, src9, 0x20, src11,
              src10, 0x20, src12, src11, 0x20, src8, src9, src10, src11);
    DUP4_ARG3(__lasx_xvpermi_q, src13, src12, 0x20, src14, src13, 0x20, src15,
              src14, 0x20, src16, src15, 0x20, src12, src13, src14, src15);
    DUP4_ARG2(__lasx_xvavg_bu, src0, src1, src2, src3, src4, src5, src6, src7,
              src0, src2, src4, src6);
    DUP4_ARG2(__lasx_xvavg_bu, src8, src9, src10, src11, src12, src13, src14,
              src15, src8, src10, src12, src14);

    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src2, dst, 0, 0);
    __lasx_xvstelm_d(src2, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src2, dst, 0, 2);
    __lasx_xvstelm_d(src2, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src4, dst, 0, 0);
    __lasx_xvstelm_d(src4, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src4, dst, 0, 2);
    __lasx_xvstelm_d(src4, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src6, dst, 0, 0);
    __lasx_xvstelm_d(src6, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src6, dst, 0, 2);
    __lasx_xvstelm_d(src6, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src8, dst, 0, 0);
    __lasx_xvstelm_d(src8, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src8, dst, 0, 2);
    __lasx_xvstelm_d(src8, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src10, dst, 0, 0);
    __lasx_xvstelm_d(src10, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src10, dst, 0, 2);
    __lasx_xvstelm_d(src10, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src12, dst, 0, 0);
    __lasx_xvstelm_d(src12, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src12, dst, 0, 2);
    __lasx_xvstelm_d(src12, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src14, dst, 0, 0);
    __lasx_xvstelm_d(src14, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src14, dst, 0, 2);
    __lasx_xvstelm_d(src14, dst, 8, 3);
}

static void common_vt_bil_no_rnd_8x16_lasx(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src8 = __lasx_xvld(_src, 0);

    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2,
              0x20, src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src5, src4, 0x20, src6, src5, 0x20, src7, src6,
              0x20, src8, src7, 0x20, src4, src5, src6, src7);
    DUP4_ARG2(__lasx_xvavg_bu, src0, src1, src2, src3, src4, src5, src6, src7,
              src0, src2, src4, src6);

    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src0, dst, 0, 2);
    __lasx_xvstelm_d(src0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src2, dst, 0, 0);
    __lasx_xvstelm_d(src2, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src2, dst, 0, 2);
    __lasx_xvstelm_d(src2, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src4, dst, 0, 0);
    __lasx_xvstelm_d(src4, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src4, dst, 0, 2);
    __lasx_xvstelm_d(src4, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(src6, dst, 0, 0);
    __lasx_xvstelm_d(src6, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(src6, dst, 0, 2);
    __lasx_xvstelm_d(src6, dst, 8, 3);
}

void ff_put_no_rnd_pixels16_y2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                      ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_vt_bil_no_rnd_16x16_lasx(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_vt_bil_no_rnd_8x16_lasx(pixels, line_size, block, line_size);
    }
}

static void common_hv_bil_no_rnd_16x16_lasx(const uint8_t *src,
                                            int32_t src_stride,
                                            uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    __m256i src10, src11, src12, src13, src14, src15, src16, src17;
    __m256i sum0, sum1, sum2, sum3, sum4, sum5, sum6, sum7;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (1 - src_stride_4x);
    src9 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src10, src11);
    src12 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src13 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src14, src15);
    src16 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src8, src17);

    DUP4_ARG3(__lasx_xvpermi_q, src0, src4, 0x02, src1, src5, 0x02, src2,
              src6, 0x02, src3, src7, 0x02, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src4, src8, 0x02, src9, src13, 0x02, src10,
              src14, 0x02, src11, src15, 0x02, src4, src5, src6, src7);
    DUP2_ARG3(__lasx_xvpermi_q, src12, src16, 0x02, src13, src17, 0x02,
              src8, src9);
    DUP4_ARG2(__lasx_xvilvl_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum0, sum2, sum4, sum6);
    DUP4_ARG2(__lasx_xvilvh_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum1, sum3, sum5, sum7);
    src8 = __lasx_xvilvl_h(src9, src4);
    src9 = __lasx_xvilvh_h(src9, src4);

    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum0, sum0, sum1, sum1, sum2, sum2,
              sum3, sum3, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum4, sum4, sum5, sum5, sum6, sum6,
              sum7, sum7, src4, src5, src6, src7);
    DUP2_ARG2(__lasx_xvhaddw_hu_bu, src8, src8, src9, src9, src8, src9);

    DUP4_ARG2(__lasx_xvadd_h, src0, src2, src1, src3, src2, src4, src3, src5,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvadd_h, src4, src6, src5, src7, src6, src8, src7, src9,
              sum4, sum5, sum6, sum7);
    DUP4_ARG2(__lasx_xvaddi_hu, sum0, 1, sum1, 1, sum2, 1, sum3, 1,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvaddi_hu, sum4, 1, sum5, 1, sum6, 1, sum7, 1,
              sum4, sum5, sum6, sum7);
    DUP4_ARG3(__lasx_xvsrani_b_h, sum1, sum0, 2, sum3, sum2, 2, sum5, sum4, 2,
              sum7, sum6, 2, sum0, sum1, sum2, sum3);
    __lasx_xvstelm_d(sum0, dst, 0, 0);
    __lasx_xvstelm_d(sum0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 0);
    __lasx_xvstelm_d(sum1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 0);
    __lasx_xvstelm_d(sum2, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 0);
    __lasx_xvstelm_d(sum3, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum0, dst, 0, 2);
    __lasx_xvstelm_d(sum0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 2);
    __lasx_xvstelm_d(sum1, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 2);
    __lasx_xvstelm_d(sum2, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 2);
    __lasx_xvstelm_d(sum3, dst, 8, 3);
    dst += dst_stride;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (1 - src_stride_4x);
    src9 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src10, src11);
    src12 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src13 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src14, src15);
    src16 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src8, src17);

    DUP4_ARG3(__lasx_xvpermi_q, src0, src4, 0x02, src1, src5, 0x02, src2, src6, 0x02,
              src3, src7, 0x02, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src4, src8, 0x02, src9, src13, 0x02, src10, src14, 0x02,
              src11, src15, 0x02, src4, src5, src6, src7);
    DUP2_ARG3(__lasx_xvpermi_q, src12, src16, 0x02, src13, src17, 0x02, src8, src9);

    DUP4_ARG2(__lasx_xvilvl_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum0, sum2, sum4, sum6);
    DUP4_ARG2(__lasx_xvilvh_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum1, sum3, sum5, sum7);
    src8 = __lasx_xvilvl_h(src9, src4);
    src9 = __lasx_xvilvh_h(src9, src4);

    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum0, sum0, sum1, sum1, sum2, sum2,
              sum3, sum3, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum4, sum4, sum5, sum5, sum6, sum6,
              sum7, sum7, src4, src5, src6, src7);
    DUP2_ARG2(__lasx_xvhaddw_hu_bu, src8, src8, src9, src9, src8, src9);

    DUP4_ARG2(__lasx_xvadd_h, src0, src2, src1, src3, src2, src4, src3, src5,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvadd_h, src4, src6, src5, src7, src6, src8, src7, src9,
              sum4, sum5, sum6, sum7);
    DUP4_ARG2(__lasx_xvaddi_hu, sum0, 1, sum1, 1, sum2, 1, sum3, 1,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvaddi_hu, sum4, 1, sum5, 1, sum6, 1, sum7, 1,
              sum4, sum5, sum6, sum7);
    DUP4_ARG3(__lasx_xvsrani_b_h, sum1, sum0, 2, sum3, sum2, 2, sum5, sum4, 2,
              sum7, sum6, 2, sum0, sum1, sum2, sum3);
    __lasx_xvstelm_d(sum0, dst, 0, 0);
    __lasx_xvstelm_d(sum0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 0);
    __lasx_xvstelm_d(sum1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 0);
    __lasx_xvstelm_d(sum2, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 0);
    __lasx_xvstelm_d(sum3, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum0, dst, 0, 2);
    __lasx_xvstelm_d(sum0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 2);
    __lasx_xvstelm_d(sum1, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 2);
    __lasx_xvstelm_d(sum2, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 2);
    __lasx_xvstelm_d(sum3, dst, 8, 3);
}

static void common_hv_bil_no_rnd_8x16_lasx(const uint8_t *src,
                                           int32_t src_stride,
                                           uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    __m256i src10, src11, src12, src13, src14, src15, src16, src17;
    __m256i sum0, sum1, sum2, sum3, sum4, sum5, sum6, sum7;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (1 - src_stride_4x);
    src9 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src10, src11);
    src12 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src13 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src14, src15);
    src16 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src8, src17);

    DUP4_ARG3(__lasx_xvpermi_q, src0, src4, 0x02, src1, src5, 0x02, src2,
              src6, 0x02, src3, src7, 0x02, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src4, src8, 0x02, src9, src13, 0x02, src10,
              src14, 0x02, src11, src15, 0x02, src4, src5, src6, src7);
    DUP2_ARG3(__lasx_xvpermi_q, src12, src16, 0x02, src13, src17, 0x02, src8, src9);

    DUP4_ARG2(__lasx_xvilvl_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum0, sum2, sum4, sum6);
    DUP4_ARG2(__lasx_xvilvh_h, src5, src0, src6, src1, src7, src2, src8, src3,
              sum1, sum3, sum5, sum7);
    src8 = __lasx_xvilvl_h(src9, src4);
    src9 = __lasx_xvilvh_h(src9, src4);

    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum0, sum0, sum1, sum1, sum2, sum2,
              sum3, sum3, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum4, sum4, sum5, sum5, sum6, sum6,
              sum7, sum7, src4, src5, src6, src7);
    DUP2_ARG2(__lasx_xvhaddw_hu_bu, src8, src8, src9, src9, src8, src9);

    DUP4_ARG2(__lasx_xvadd_h, src0, src2, src1, src3, src2, src4, src3, src5,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvadd_h, src4, src6, src5, src7, src6, src8, src7, src9,
              sum4, sum5, sum6, sum7);
    DUP4_ARG2(__lasx_xvaddi_hu, sum0, 1, sum1, 1, sum2, 1, sum3, 1,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvaddi_hu, sum4, 1, sum5, 1, sum6, 1, sum7, 1,
              sum4, sum5, sum6, sum7);
    DUP4_ARG3(__lasx_xvsrani_b_h, sum1, sum0, 2, sum3, sum2, 2, sum5, sum4, 2,
              sum7, sum6, 2, sum0, sum1, sum2, sum3);
    __lasx_xvstelm_d(sum0, dst, 0, 0);
    __lasx_xvstelm_d(sum0, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 0);
    __lasx_xvstelm_d(sum1, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 0);
    __lasx_xvstelm_d(sum2, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 0);
    __lasx_xvstelm_d(sum3, dst, 8, 1);
    dst += dst_stride;
    __lasx_xvstelm_d(sum0, dst, 0, 2);
    __lasx_xvstelm_d(sum0, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum1, dst, 0, 2);
    __lasx_xvstelm_d(sum1, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum2, dst, 0, 2);
    __lasx_xvstelm_d(sum2, dst, 8, 3);
    dst += dst_stride;
    __lasx_xvstelm_d(sum3, dst, 0, 2);
    __lasx_xvstelm_d(sum3, dst, 8, 3);
}

void ff_put_no_rnd_pixels16_xy2_8_lasx(uint8_t *block,
                                       const uint8_t *pixels,
                                       ptrdiff_t line_size, int h)
{
    if (h == 16) {
        common_hv_bil_no_rnd_16x16_lasx(pixels, line_size, block, line_size);
    } else if (h == 8) {
        common_hv_bil_no_rnd_8x16_lasx(pixels, line_size, block, line_size);
    }
}

static void common_hz_bil_no_rnd_8x8_lasx(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i src8, src9, src10, src11, src12, src13, src14, src15;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_4x = dst_stride << 2;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (1 - src_stride_4x);
    src8 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src9, src10);
    src11 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src12 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src13, src14);
    src15 = __lasx_xvldx(_src, src_stride_3x);

    DUP4_ARG2(__lasx_xvpickev_d, src1, src0, src3, src2, src5, src4, src7,
              src6, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvpickev_d, src9, src8, src11, src10, src13, src12, src15,
              src14, src4, src5, src6, src7);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src5, src4,
              0x20, src7, src6, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src2);
    src1 = __lasx_xvavg_bu(src1, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src0, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src1, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src1, dst + dst_stride_3x, 0, 3);
}

static void common_hz_bil_no_rnd_4x8_lasx(const uint8_t *src,
                                          int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    uint8_t *_src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    DUP4_ARG2(__lasx_xvpickev_d, src1, src0, src3, src2, src5, src4, src7, src6,
              src0, src1, src2, src3);
    DUP2_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src3, src2, 0x20, src0, src1);
    src0 = __lasx_xvavg_bu(src0, src1);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src0, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, dst + dst_stride_3x, 0, 3);
}

void ff_put_no_rnd_pixels8_x2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_hz_bil_no_rnd_8x8_lasx(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_hz_bil_no_rnd_4x8_lasx(pixels, line_size, block, line_size);
    }
}

static void common_vt_bil_no_rnd_8x8_lasx(const uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_4x = dst_stride << 2;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src8 = __lasx_xvld(_src, 0);

    DUP4_ARG2(__lasx_xvpickev_d, src1, src0, src2, src1, src3, src2, src4, src3,
              src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvpickev_d, src5, src4, src6, src5, src7, src6, src8, src7,
              src4, src5, src6, src7);
    DUP4_ARG3(__lasx_xvpermi_q, src2, src0, 0x20, src3, src1, 0x20, src6, src4,
              0x20, src7, src5, 0x20, src0, src1, src2, src3);
    src0 = __lasx_xvavg_bu(src0, src1);
    src1 = __lasx_xvavg_bu(src2, src3);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src0, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lasx_xvstelm_d(src1, dst, 0, 0);
    __lasx_xvstelm_d(src1, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src1, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src1, dst + dst_stride_3x, 0, 3);
}

static void common_vt_bil_no_rnd_4x8_lasx(const uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP4_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, _src,
              src_stride_3x, _src, src_stride_4x, src1, src2, src3, src4);
    DUP4_ARG2(__lasx_xvpickev_d, src1, src0, src2, src1, src3, src2, src4, src3,
              src0, src1, src2, src3);
    DUP2_ARG3(__lasx_xvpermi_q, src2, src0, 0x20, src3, src1, 0x20, src0, src1);
    src0 = __lasx_xvavg_bu(src0, src1);
    __lasx_xvstelm_d(src0, dst, 0, 0);
    __lasx_xvstelm_d(src0, dst + dst_stride, 0, 1);
    __lasx_xvstelm_d(src0, dst + dst_stride_2x, 0, 2);
    __lasx_xvstelm_d(src0, dst + dst_stride_3x, 0, 3);
}

void ff_put_no_rnd_pixels8_y2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                     ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_vt_bil_no_rnd_8x8_lasx(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_vt_bil_no_rnd_4x8_lasx(pixels, line_size, block, line_size);
    }
}

static void common_hv_bil_no_rnd_8x8_lasx(const uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i src8, src9, src10, src11, src12, src13, src14, src15, src16, src17;
    __m256i sum0, sum1, sum2, sum3;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_4x = dst_stride << 2;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src4 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
    src7 = __lasx_xvldx(_src, src_stride_3x);
    _src += (1 - src_stride_4x);
    src9 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src10, src11);
    src12 = __lasx_xvldx(_src, src_stride_3x);
    _src += src_stride_4x;
    src13 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
              src14, src15);
    src16 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src8, src17);

    DUP4_ARG2(__lasx_xvilvl_b, src9, src0, src10, src1, src11, src2, src12, src3,
              src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvilvl_b, src13, src4, src14, src5, src15, src6, src16, src7,
              src4, src5, src6, src7);
    src8 = __lasx_xvilvl_b(src17, src8);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2,
              0x20, src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG3(__lasx_xvpermi_q, src5, src4, 0x20, src6, src5, 0x20, src7, src6,
              0x20, src8, src7, 0x20, src4, src5, src6, src7);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, src0, src0, src1, src1, src2, src2,
              src3, src3, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, src4, src4, src5, src5, src6, src6,
              src7, src7, src4, src5, src6, src7);
    DUP4_ARG2(__lasx_xvadd_h, src0, src1, src2, src3, src4, src5, src6, src7,
              sum0, sum1, sum2, sum3);
    DUP4_ARG2(__lasx_xvaddi_hu, sum0, 1, sum1, 1, sum2, 1, sum3, 1,
              sum0, sum1, sum2, sum3);
    DUP2_ARG3(__lasx_xvsrani_b_h, sum1, sum0, 2, sum3, sum2, 2, sum0, sum1);
    __lasx_xvstelm_d(sum0, dst, 0, 0);
    __lasx_xvstelm_d(sum0, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(sum0, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(sum0, dst + dst_stride_3x, 0, 3);
    dst += dst_stride_4x;
    __lasx_xvstelm_d(sum1, dst, 0, 0);
    __lasx_xvstelm_d(sum1, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(sum1, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(sum1, dst + dst_stride_3x, 0, 3);
}

static void common_hv_bil_no_rnd_4x8_lasx(const uint8_t *src, int32_t src_stride,
                                          uint8_t *dst, int32_t dst_stride)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i src8, src9, sum0, sum1;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t *_src = (uint8_t*)src;

    src0 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
    src3 = __lasx_xvldx(_src, src_stride_3x);
    _src += 1;
    src5 = __lasx_xvld(_src, 0);
    DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src6, src7);
    src8 = __lasx_xvldx(_src, src_stride_3x);
    _src += (src_stride_4x - 1);
    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src4, src9);

    DUP4_ARG2(__lasx_xvilvl_b, src5, src0, src6, src1, src7, src2, src8, src3,
              src0, src1, src2, src3);
    src4 = __lasx_xvilvl_b(src9, src4);
    DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2,
              0x20, src4, src3, 0x20, src0, src1, src2, src3);
    DUP4_ARG2(__lasx_xvhaddw_hu_bu, src0, src0, src1, src1, src2, src2,
              src3, src3, src0, src1, src2, src3);
    DUP2_ARG2(__lasx_xvadd_h, src0, src1, src2, src3, sum0, sum1);
    sum0 = __lasx_xvaddi_hu(sum0, 1);
    sum1 = __lasx_xvaddi_hu(sum1, 1);
    sum0 = __lasx_xvsrani_b_h(sum1, sum0, 2);
    __lasx_xvstelm_d(sum0, dst, 0, 0);
    __lasx_xvstelm_d(sum0, dst + dst_stride, 0, 2);
    __lasx_xvstelm_d(sum0, dst + dst_stride_2x, 0, 1);
    __lasx_xvstelm_d(sum0, dst + dst_stride_3x, 0, 3);
}

void ff_put_no_rnd_pixels8_xy2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                      ptrdiff_t line_size, int h)
{
    if (h == 8) {
        common_hv_bil_no_rnd_8x8_lasx(pixels, line_size, block, line_size);
    } else if (h == 4) {
        common_hv_bil_no_rnd_4x8_lasx(pixels, line_size, block, line_size);
    }
}

static void common_hv_bil_16w_lasx(const uint8_t *src, int32_t src_stride,
                                   uint8_t *dst, int32_t dst_stride,
                                   uint8_t height)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9;
    __m256i src10, src11, src12, src13, src14, src15, src16, src17;
    __m256i sum0, sum1, sum2, sum3, sum4, sum5, sum6, sum7;
    uint8_t loop_cnt;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        src0 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src1, src2);
        src3 = __lasx_xvldx(_src, src_stride_3x);
        _src += src_stride_4x;
        src4 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src5, src6);
        src7 = __lasx_xvldx(_src, src_stride_3x);
        _src += (1 - src_stride_4x);
        src9 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
                  src10, src11);
        src12 = __lasx_xvldx(_src, src_stride_3x);
        _src += src_stride_4x;
        src13 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x,
                  src14, src15);
        src16 = __lasx_xvldx(_src, src_stride_3x);
        _src += (src_stride_4x - 1);
        DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src8, src17);

        DUP4_ARG3(__lasx_xvpermi_q, src0, src4, 0x02, src1, src5, 0x02, src2,
                  src6, 0x02, src3, src7, 0x02, src0, src1, src2, src3);
        DUP4_ARG3(__lasx_xvpermi_q, src4, src8, 0x02, src9, src13, 0x02, src10,
                  src14, 0x02, src11, src15, 0x02, src4, src5, src6, src7);
        DUP2_ARG3(__lasx_xvpermi_q, src12, src16, 0x02, src13, src17, 0x02,
                   src8, src9);

        DUP4_ARG2(__lasx_xvilvl_h, src5, src0, src6, src1, src7, src2, src8,
                  src3, sum0, sum2, sum4, sum6);
        DUP4_ARG2(__lasx_xvilvh_h, src5, src0, src6, src1, src7, src2, src8,
                  src3, sum1, sum3, sum5, sum7);
        src8 = __lasx_xvilvl_h(src9, src4);
        src9 = __lasx_xvilvh_h(src9, src4);

        DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum0, sum0, sum1, sum1, sum2, sum2,
                  sum3, sum3, src0, src1, src2, src3);
        DUP4_ARG2(__lasx_xvhaddw_hu_bu, sum4, sum4, sum5, sum5, sum6, sum6,
                  sum7, sum7, src4, src5, src6, src7);
        DUP2_ARG2(__lasx_xvhaddw_hu_bu, src8, src8, src9, src9, src8, src9);

        DUP4_ARG2(__lasx_xvadd_h, src0, src2, src1, src3, src2, src4, src3,
                  src5, sum0, sum1, sum2, sum3);
        DUP4_ARG2(__lasx_xvadd_h, src4, src6, src5, src7, src6, src8, src7,
                  src9, sum4, sum5, sum6, sum7);
        DUP4_ARG3(__lasx_xvsrarni_b_h, sum1, sum0, 2, sum3, sum2, 2, sum5,
                  sum4, 2, sum7, sum6, 2, sum0, sum1, sum2, sum3);
        __lasx_xvstelm_d(sum0, dst, 0, 0);
        __lasx_xvstelm_d(sum0, dst, 8, 1);
        dst += dst_stride;
        __lasx_xvstelm_d(sum1, dst, 0, 0);
        __lasx_xvstelm_d(sum1, dst, 8, 1);
        dst += dst_stride;
        __lasx_xvstelm_d(sum2, dst, 0, 0);
        __lasx_xvstelm_d(sum2, dst, 8, 1);
        dst += dst_stride;
        __lasx_xvstelm_d(sum3, dst, 0, 0);
        __lasx_xvstelm_d(sum3, dst, 8, 1);
        dst += dst_stride;
        __lasx_xvstelm_d(sum0, dst, 0, 2);
        __lasx_xvstelm_d(sum0, dst, 8, 3);
        dst += dst_stride;
        __lasx_xvstelm_d(sum1, dst, 0, 2);
        __lasx_xvstelm_d(sum1, dst, 8, 3);
        dst += dst_stride;
        __lasx_xvstelm_d(sum2, dst, 0, 2);
        __lasx_xvstelm_d(sum2, dst, 8, 3);
        dst += dst_stride;
        __lasx_xvstelm_d(sum3, dst, 0, 2);
        __lasx_xvstelm_d(sum3, dst, 8, 3);
        dst += dst_stride;
    }
}

void ff_put_pixels16_xy2_8_lasx(uint8_t *block, const uint8_t *pixels,
                                ptrdiff_t line_size, int h)
{
    common_hv_bil_16w_lasx(pixels, line_size, block, line_size, h);
}

static void common_hv_bil_8w_lasx(const uint8_t *src, int32_t src_stride,
                                  uint8_t *dst, int32_t dst_stride,
                                  uint8_t height)
{
    __m256i src0, src1, src2, src3, src4, src5, src6, src7;
    __m256i src8, src9, sum0, sum1;
    uint8_t loop_cnt;
    int32_t src_stride_2x = src_stride << 1;
    int32_t src_stride_4x = src_stride << 2;
    int32_t dst_stride_2x = dst_stride << 1;
    int32_t dst_stride_4x = dst_stride << 2;
    int32_t dst_stride_3x = dst_stride_2x + dst_stride;
    int32_t src_stride_3x = src_stride_2x + src_stride;
    uint8_t* _src = (uint8_t*)src;

    DUP2_ARG2(__lasx_xvld, _src, 0, _src, 1, src0, src5);
    _src += src_stride;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        src1 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src2, src3);
        src4 = __lasx_xvldx(_src, src_stride_3x);
        _src += 1;
        src6 = __lasx_xvld(_src, 0);
        DUP2_ARG2(__lasx_xvldx, _src, src_stride, _src, src_stride_2x, src7, src8);
        src9 = __lasx_xvldx(_src, src_stride_3x);
        _src += (src_stride_4x - 1);
        DUP4_ARG2(__lasx_xvilvl_b, src5, src0, src6, src1, src7, src2, src8, src3,
                  src0, src1, src2, src3);
        src5 = __lasx_xvilvl_b(src9, src4);
        DUP4_ARG3(__lasx_xvpermi_q, src1, src0, 0x20, src2, src1, 0x20, src3, src2,
                  0x20, src5, src3, 0x20, src0, src1, src2, src3);
        DUP4_ARG2(__lasx_xvhaddw_hu_bu, src0, src0, src1, src1, src2, src2,
                  src3, src3, src0, src1, src2, src3);
        DUP2_ARG2(__lasx_xvadd_h, src0, src1, src2, src3, sum0, sum1);
        sum0 = __lasx_xvsrarni_b_h(sum1, sum0, 2);
        __lasx_xvstelm_d(sum0, dst, 0, 0);
        __lasx_xvstelm_d(sum0, dst + dst_stride, 0, 2);
        __lasx_xvstelm_d(sum0, dst + dst_stride_2x, 0, 1);
        __lasx_xvstelm_d(sum0, dst + dst_stride_3x, 0, 3);
        dst += dst_stride_4x;
        src0 = src4;
        src5 = src9;
    }
}

void ff_put_pixels8_xy2_8_lasx(uint8_t *block, const uint8_t *pixels,
                               ptrdiff_t line_size, int h)
{
    common_hv_bil_8w_lasx(pixels, line_size, block, line_size, h);
}
