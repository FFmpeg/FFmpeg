/*
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 * Contributed by Hao Chen <chenhao@loongson.cn>
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
#include "h264_intrapred_lasx.h"

#define PRED16X16_PLANE                                                        \
    ptrdiff_t stride_1, stride_2, stride_3, stride_4, stride_5, stride_6;      \
    ptrdiff_t stride_8, stride_15;                                             \
    int32_t res0, res1, res2, res3, cnt;                                       \
    uint8_t *src0, *src1;                                                      \
    __m256i reg0, reg1, reg2, reg3, reg4;                                      \
    __m256i tmp0, tmp1, tmp2, tmp3;                                            \
    __m256i shuff = {0x0B040A0509060807, 0x0F000E010D020C03, 0, 0};            \
    __m256i mult = {0x0004000300020001, 0x0008000700060005, 0, 0};             \
    __m256i int_mult1 = {0x0000000100000000, 0x0000000300000002,               \
                         0x0000000500000004, 0x0000000700000006};              \
                                                                               \
    stride_1 = -stride;                                                        \
    stride_2 = stride << 1;                                                    \
    stride_3 = stride_2 + stride;                                              \
    stride_4 = stride_2 << 1;                                                  \
    stride_5 = stride_4 + stride;                                              \
    stride_6 = stride_3 << 1;                                                  \
    stride_8 = stride_4 << 1;                                                  \
    stride_15 = (stride_8 << 1) - stride;                                      \
    src0 = src - 1;                                                            \
    src1 = src0 + stride_8;                                                    \
                                                                               \
    reg0 = __lasx_xvldx(src0, -stride);                                        \
    reg1 = __lasx_xvldx(src, (8 - stride));                                    \
    reg0 = __lasx_xvilvl_d(reg1, reg0);                                        \
    reg0 = __lasx_xvshuf_b(reg0, reg0, shuff);                                 \
    reg0 = __lasx_xvhsubw_hu_bu(reg0, reg0);                                   \
    reg0 = __lasx_xvmul_h(reg0, mult);                                         \
    res1 = (src1[0] - src0[stride_6]) +                                        \
        2 * (src1[stride] - src0[stride_5]) +                                  \
        3 * (src1[stride_2] - src0[stride_4]) +                                \
        4 * (src1[stride_3] - src0[stride_3]) +                                \
        5 * (src1[stride_4] - src0[stride_2]) +                                \
        6 * (src1[stride_5] - src0[stride]) +                                  \
        7 * (src1[stride_6] - src0[0]) +                                       \
        8 * (src0[stride_15] - src0[stride_1]);                                \
    reg0 = __lasx_xvhaddw_w_h(reg0, reg0);                                     \
    reg0 = __lasx_xvhaddw_d_w(reg0, reg0);                                     \
    reg0 = __lasx_xvhaddw_q_d(reg0, reg0);                                     \
    res0 = __lasx_xvpickve2gr_w(reg0, 0);                                      \

#define PRED16X16_PLANE_END                                                    \
    res2 = (src0[stride_15] + src[15 - stride] + 1) << 4;                      \
    res3 = 7 * (res0 + res1);                                                  \
    res2 -= res3;                                                              \
    reg0 = __lasx_xvreplgr2vr_w(res0);                                         \
    reg1 = __lasx_xvreplgr2vr_w(res1);                                         \
    reg2 = __lasx_xvreplgr2vr_w(res2);                                         \
    reg3 = __lasx_xvmul_w(reg0, int_mult1);                                    \
    reg4 = __lasx_xvslli_w(reg0, 3);                                           \
    reg4 = __lasx_xvadd_w(reg4, reg3);                                         \
    for (cnt = 8; cnt--;) {                                                    \
        tmp0 = __lasx_xvadd_w(reg2, reg3);                                     \
        tmp1 = __lasx_xvadd_w(reg2, reg4);                                     \
        tmp0 = __lasx_xvssrani_hu_w(tmp1, tmp0, 5);                            \
        tmp0 = __lasx_xvpermi_d(tmp0, 0xD8);                                   \
        reg2 = __lasx_xvadd_w(reg2, reg1);                                     \
        tmp2 = __lasx_xvadd_w(reg2, reg3);                                     \
        tmp3 = __lasx_xvadd_w(reg2, reg4);                                     \
        tmp1 = __lasx_xvssrani_hu_w(tmp3, tmp2, 5);                            \
        tmp1 = __lasx_xvpermi_d(tmp1, 0xD8);                                   \
        tmp0 = __lasx_xvssrani_bu_h(tmp1, tmp0, 0);                            \
        reg2 = __lasx_xvadd_w(reg2, reg1);                                     \
        __lasx_xvstelm_d(tmp0, src, 0, 0);                                     \
        __lasx_xvstelm_d(tmp0, src, 8, 2);                                     \
        src += stride;                                                         \
        __lasx_xvstelm_d(tmp0, src, 0, 1);                                     \
        __lasx_xvstelm_d(tmp0, src, 8, 3);                                     \
        src += stride;                                                         \
    }


void ff_h264_pred16x16_plane_h264_8_lasx(uint8_t *src, ptrdiff_t stride)
{
    PRED16X16_PLANE
    res0 = (5 * res0 + 32) >> 6;
    res1 = (5 * res1 + 32) >> 6;
    PRED16X16_PLANE_END
}

void ff_h264_pred16x16_plane_rv40_8_lasx(uint8_t *src, ptrdiff_t stride)
{
    PRED16X16_PLANE
    res0 = (res0 + (res0 >> 2)) >> 4;
    res1 = (res1 + (res1 >> 2)) >> 4;
    PRED16X16_PLANE_END
}

void ff_h264_pred16x16_plane_svq3_8_lasx(uint8_t *src, ptrdiff_t stride)
{
    PRED16X16_PLANE
    cnt  = (5 * (res0/4)) / 16;
    res0 = (5 * (res1/4)) / 16;
    res1 = cnt;
    PRED16X16_PLANE_END
}
