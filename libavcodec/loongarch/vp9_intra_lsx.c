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

#include "libavcodec/vp9dsp.h"
#include "libavutil/loongarch/loongson_intrinsics.h"
#include "vp9dsp_loongarch.h"

#define LSX_ST_8(_dst0, _dst1, _dst2, _dst3, _dst4,   \
                 _dst5, _dst6, _dst7, _dst, _stride,  \
                 _stride2, _stride3, _stride4)        \
{                                                     \
    __lsx_vst(_dst0, _dst, 0);                        \
    __lsx_vstx(_dst1, _dst, _stride);                 \
    __lsx_vstx(_dst2, _dst, _stride2);                \
    __lsx_vstx(_dst3, _dst, _stride3);                \
    _dst += _stride4;                                 \
    __lsx_vst(_dst4, _dst, 0);                        \
    __lsx_vstx(_dst5, _dst, _stride);                 \
    __lsx_vstx(_dst6, _dst, _stride2);                \
    __lsx_vstx(_dst7, _dst, _stride3);                \
}

#define LSX_ST_8X16(_dst0, _dst1, _dst2, _dst3, _dst4,   \
                    _dst5, _dst6, _dst7, _dst, _stride)  \
{                                                        \
    __lsx_vst(_dst0, _dst, 0);                           \
    __lsx_vst(_dst0, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst1, _dst, 0);                           \
    __lsx_vst(_dst1, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst2, _dst, 0);                           \
    __lsx_vst(_dst2, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst3, _dst, 0);                           \
    __lsx_vst(_dst3, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst4, _dst, 0);                           \
    __lsx_vst(_dst4, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst5, _dst, 0);                           \
    __lsx_vst(_dst5, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst6, _dst, 0);                           \
    __lsx_vst(_dst6, _dst, 16);                          \
    _dst += _stride;                                     \
    __lsx_vst(_dst7, _dst, 0);                           \
    __lsx_vst(_dst7, _dst, 16);                          \
    _dst += _stride;                                     \
}

void ff_vert_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *left,
                       const uint8_t *src)
{
    __m128i src0;
    ptrdiff_t stride2 = dst_stride << 1;
    ptrdiff_t stride3 = stride2 + dst_stride;
    ptrdiff_t stride4 = stride2 << 1;
    src0 = __lsx_vld(src, 0);
    LSX_ST_8(src0, src0, src0, src0, src0, src0, src0, src0, dst,
             dst_stride, stride2, stride3, stride4);
    dst += stride4;
    LSX_ST_8(src0, src0, src0, src0, src0, src0, src0, src0, dst,
             dst_stride, stride2, stride3, stride4);
}

void ff_vert_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *left,
                       const uint8_t *src)
{
    uint32_t row;
    __m128i src0, src1;

    DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src1);
    for (row = 32; row--;) {
        __lsx_vst(src0, dst, 0);
        __lsx_vst(src1, dst, 16);
        dst += dst_stride;
    }
}

void ff_hor_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src,
                      const uint8_t *top)
{
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i src8, src9, src10, src11, src12, src13, src14, src15;
    ptrdiff_t stride2 = dst_stride << 1;
    ptrdiff_t stride3 = stride2 + dst_stride;
    ptrdiff_t stride4 = stride2 << 1;

    src15 = __lsx_vldrepl_b(src, 0);
    src14 = __lsx_vldrepl_b(src, 1);
    src13 = __lsx_vldrepl_b(src, 2);
    src12 = __lsx_vldrepl_b(src, 3);
    src11 = __lsx_vldrepl_b(src, 4);
    src10 = __lsx_vldrepl_b(src, 5);
    src9  = __lsx_vldrepl_b(src, 6);
    src8  = __lsx_vldrepl_b(src, 7);
    src7  = __lsx_vldrepl_b(src, 8);
    src6  = __lsx_vldrepl_b(src, 9);
    src5  = __lsx_vldrepl_b(src, 10);
    src4  = __lsx_vldrepl_b(src, 11);
    src3  = __lsx_vldrepl_b(src, 12);
    src2  = __lsx_vldrepl_b(src, 13);
    src1  = __lsx_vldrepl_b(src, 14);
    src0  = __lsx_vldrepl_b(src, 15);
    LSX_ST_8(src0, src1, src2, src3, src4, src5, src6, src7, dst,
             dst_stride, stride2, stride3, stride4);
    dst += stride4;
    LSX_ST_8(src8, src9, src10, src11, src12, src13, src14, src15, dst,
             dst_stride, stride2, stride3, stride4);
}

void ff_hor_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src,
                      const uint8_t *top)
{
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i src8, src9, src10, src11, src12, src13, src14, src15;
    __m128i src16, src17, src18, src19, src20, src21, src22, src23;
    __m128i src24, src25, src26, src27, src28, src29, src30, src31;

    src31 = __lsx_vldrepl_b(src, 0);
    src30 = __lsx_vldrepl_b(src, 1);
    src29 = __lsx_vldrepl_b(src, 2);
    src28 = __lsx_vldrepl_b(src, 3);
    src27 = __lsx_vldrepl_b(src, 4);
    src26 = __lsx_vldrepl_b(src, 5);
    src25 = __lsx_vldrepl_b(src, 6);
    src24 = __lsx_vldrepl_b(src, 7);
    src23 = __lsx_vldrepl_b(src, 8);
    src22 = __lsx_vldrepl_b(src, 9);
    src21 = __lsx_vldrepl_b(src, 10);
    src20 = __lsx_vldrepl_b(src, 11);
    src19 = __lsx_vldrepl_b(src, 12);
    src18 = __lsx_vldrepl_b(src, 13);
    src17 = __lsx_vldrepl_b(src, 14);
    src16 = __lsx_vldrepl_b(src, 15);
    src15 = __lsx_vldrepl_b(src, 16);
    src14 = __lsx_vldrepl_b(src, 17);
    src13 = __lsx_vldrepl_b(src, 18);
    src12 = __lsx_vldrepl_b(src, 19);
    src11 = __lsx_vldrepl_b(src, 20);
    src10 = __lsx_vldrepl_b(src, 21);
    src9  = __lsx_vldrepl_b(src, 22);
    src8  = __lsx_vldrepl_b(src, 23);
    src7  = __lsx_vldrepl_b(src, 24);
    src6  = __lsx_vldrepl_b(src, 25);
    src5  = __lsx_vldrepl_b(src, 26);
    src4  = __lsx_vldrepl_b(src, 27);
    src3  = __lsx_vldrepl_b(src, 28);
    src2  = __lsx_vldrepl_b(src, 29);
    src1  = __lsx_vldrepl_b(src, 30);
    src0  = __lsx_vldrepl_b(src, 31);
    LSX_ST_8X16(src0, src1, src2, src3, src4, src5, src6, src7,
                dst, dst_stride);
    LSX_ST_8X16(src8, src9, src10, src11, src12, src13, src14, src15,
                dst, dst_stride);
    LSX_ST_8X16(src16, src17, src18, src19, src20, src21, src22, src23,
                dst, dst_stride);
    LSX_ST_8X16(src24, src25, src26, src27, src28, src29, src30, src31,
                dst, dst_stride);
}

void ff_dc_4x4_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src_left,
                   const uint8_t *src_top)
{
    __m128i tmp0, tmp1, dst0;

    tmp0 = __lsx_vldrepl_w(src_top, 0);
    tmp1 = __lsx_vldrepl_w(src_left, 0);
    dst0 = __lsx_vilvl_w(tmp1, tmp0);
    dst0 = __lsx_vhaddw_hu_bu(dst0, dst0);
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);
    dst0 = __lsx_vsrari_w(dst0, 3);
    dst0 = __lsx_vshuf4i_b(dst0, 0);
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 0);
}

#define INTRA_DC_TL_4X4(dir)                                            \
void ff_dc_##dir##_4x4_lsx(uint8_t *dst, ptrdiff_t dst_stride,          \
                          const uint8_t *left,                          \
                          const uint8_t *top)                           \
{                                                                       \
    __m128i tmp0, dst0;                                                 \
                                                                        \
    tmp0 = __lsx_vldrepl_w(dir, 0);                                     \
    dst0 = __lsx_vhaddw_hu_bu(tmp0, tmp0);                              \
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);                              \
    dst0 = __lsx_vsrari_w(dst0, 2);                                     \
    dst0 = __lsx_vshuf4i_b(dst0, 0);                                    \
    __lsx_vstelm_w(dst0, dst, 0, 0);                                    \
    dst += dst_stride;                                                  \
    __lsx_vstelm_w(dst0, dst, 0, 0);                                    \
    dst += dst_stride;                                                  \
    __lsx_vstelm_w(dst0, dst, 0, 0);                                    \
    dst += dst_stride;                                                  \
    __lsx_vstelm_w(dst0, dst, 0, 0);                                    \
}
INTRA_DC_TL_4X4(top);
INTRA_DC_TL_4X4(left);

void ff_dc_8x8_lsx(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src_left,
                   const uint8_t *src_top)
{
    __m128i tmp0, tmp1, dst0;

    tmp0 = __lsx_vldrepl_d(src_top, 0);
    tmp1 = __lsx_vldrepl_d(src_left, 0);
    dst0 = __lsx_vilvl_d(tmp1, tmp0);
    dst0 = __lsx_vhaddw_hu_bu(dst0, dst0);
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);
    dst0 = __lsx_vhaddw_qu_du(dst0, dst0);
    dst0 = __lsx_vsrari_w(dst0, 4);
    dst0 = __lsx_vreplvei_b(dst0, 0);
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(dst0, dst, 0, 0);
}

#define INTRA_DC_TL_8X8(dir)                                                  \
void ff_dc_##dir##_8x8_lsx(uint8_t *dst, ptrdiff_t dst_stride,                \
                           const uint8_t *left,                               \
                           const uint8_t *top)                                \
{                                                                             \
    __m128i tmp0, dst0;                                                       \
                                                                              \
    tmp0 = __lsx_vldrepl_d(dir, 0);                                           \
    dst0 = __lsx_vhaddw_hu_bu(tmp0, tmp0);                                    \
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);                                    \
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);                                    \
    dst0 = __lsx_vsrari_w(dst0, 3);                                           \
    dst0 = __lsx_vreplvei_b(dst0, 0);                                         \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
    dst += dst_stride;                                                        \
    __lsx_vstelm_d(dst0, dst, 0, 0);                                          \
}

INTRA_DC_TL_8X8(top);
INTRA_DC_TL_8X8(left);

void ff_dc_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top)
{
    __m128i tmp0, tmp1, dst0;
    ptrdiff_t stride2 = dst_stride << 1;
    ptrdiff_t stride3 = stride2 + dst_stride;
    ptrdiff_t stride4 = stride2 << 1;

    tmp0 = __lsx_vld(src_top, 0);
    tmp1 = __lsx_vld(src_left, 0);
    DUP2_ARG2(__lsx_vhaddw_hu_bu, tmp0, tmp0, tmp1, tmp1, tmp0, tmp1);
    dst0 = __lsx_vadd_h(tmp0, tmp1);
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);
    dst0 = __lsx_vhaddw_qu_du(dst0, dst0);
    dst0 = __lsx_vsrari_w(dst0, 5);
    dst0 = __lsx_vreplvei_b(dst0, 0);
    LSX_ST_8(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst,
             dst_stride, stride2, stride3, stride4);
    dst += stride4;
    LSX_ST_8(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst,
             dst_stride, stride2, stride3, stride4);
}

#define INTRA_DC_TL_16X16(dir)                                                \
void ff_dc_##dir##_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride,              \
                             const uint8_t *left,                             \
                             const uint8_t *top)                              \
{                                                                             \
    __m128i tmp0, dst0;                                                       \
    ptrdiff_t stride2 = dst_stride << 1;                                      \
    ptrdiff_t stride3 = stride2 + dst_stride;                                 \
    ptrdiff_t stride4 = stride2 << 1;                                         \
                                                                              \
    tmp0 = __lsx_vld(dir, 0);                                                 \
    dst0 = __lsx_vhaddw_hu_bu(tmp0, tmp0);                                    \
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);                                    \
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);                                    \
    dst0 = __lsx_vhaddw_qu_du(dst0, dst0);                                    \
    dst0 = __lsx_vsrari_w(dst0, 4);                                           \
    dst0 = __lsx_vreplvei_b(dst0, 0);                                         \
    LSX_ST_8(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst,             \
             dst_stride, stride2, stride3, stride4);                          \
    dst += stride4;                                                           \
    LSX_ST_8(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst,             \
             dst_stride, stride2, stride3, stride4);                          \
}

INTRA_DC_TL_16X16(top);
INTRA_DC_TL_16X16(left);

void ff_dc_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top)
{
    __m128i tmp0, tmp1, tmp2, tmp3, dst0;

    DUP2_ARG2(__lsx_vld, src_top, 0, src_top, 16, tmp0, tmp1);
    DUP2_ARG2(__lsx_vld, src_left, 0, src_left, 16, tmp2, tmp3);
    DUP4_ARG2(__lsx_vhaddw_hu_bu, tmp0, tmp0, tmp1, tmp1, tmp2, tmp2,
              tmp3, tmp3, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vadd_h, tmp0, tmp1, tmp2, tmp3, tmp0, tmp1);
    dst0 = __lsx_vadd_h(tmp0, tmp1);
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);
    dst0 = __lsx_vhaddw_qu_du(dst0, dst0);
    dst0 = __lsx_vsrari_w(dst0, 6);
    dst0 = __lsx_vreplvei_b(dst0, 0);
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,
                dst, dst_stride);
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,
                dst, dst_stride);
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,
                dst, dst_stride);
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,
                dst, dst_stride);
}

#define INTRA_DC_TL_32X32(dir)                                               \
void ff_dc_##dir##_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride,             \
                             const uint8_t *left,                            \
                             const uint8_t *top)                             \
{                                                                            \
    __m128i tmp0, tmp1, dst0;                                                \
                                                                             \
    DUP2_ARG2(__lsx_vld, dir, 0, dir, 16, tmp0, tmp1);                       \
    DUP2_ARG2(__lsx_vhaddw_hu_bu, tmp0, tmp0, tmp1, tmp1, tmp0, tmp1);       \
    dst0 = __lsx_vadd_h(tmp0, tmp1);                                         \
    dst0 = __lsx_vhaddw_wu_hu(dst0, dst0);                                   \
    dst0 = __lsx_vhaddw_du_wu(dst0, dst0);                                   \
    dst0 = __lsx_vhaddw_qu_du(dst0, dst0);                                   \
    dst0 = __lsx_vsrari_w(dst0, 5);                                          \
    dst0 = __lsx_vreplvei_b(dst0, 0);                                        \
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,              \
                dst, dst_stride);                                            \
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,              \
                dst, dst_stride);                                            \
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,              \
                dst, dst_stride);                                            \
    LSX_ST_8X16(dst0, dst0, dst0, dst0, dst0, dst0, dst0, dst0,              \
                dst, dst_stride);                                            \
}

INTRA_DC_TL_32X32(top);
INTRA_DC_TL_32X32(left);

#define INTRA_PREDICT_VALDC_16X16_LSX(val)                             \
void ff_dc_##val##_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride,       \
                             const uint8_t *left, const uint8_t *top)  \
{                                                                      \
    __m128i out = __lsx_vldi(val);                                     \
    ptrdiff_t stride2 = dst_stride << 1;                               \
    ptrdiff_t stride3 = stride2 + dst_stride;                          \
    ptrdiff_t stride4 = stride2 << 1;                                  \
                                                                       \
    LSX_ST_8(out, out, out, out, out, out, out, out, dst,              \
             dst_stride, stride2, stride3, stride4);                   \
    dst += stride4;                                                    \
    LSX_ST_8(out, out, out, out, out, out, out, out, dst,              \
             dst_stride, stride2, stride3, stride4);                   \
}

INTRA_PREDICT_VALDC_16X16_LSX(127);
INTRA_PREDICT_VALDC_16X16_LSX(128);
INTRA_PREDICT_VALDC_16X16_LSX(129);

#define INTRA_PREDICT_VALDC_32X32_LSX(val)                               \
void ff_dc_##val##_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride,         \
                             const uint8_t *left, const uint8_t *top)    \
{                                                                        \
    __m128i out = __lsx_vldi(val);                                       \
                                                                         \
    LSX_ST_8X16(out, out, out, out, out, out, out, out, dst, dst_stride);\
    LSX_ST_8X16(out, out, out, out, out, out, out, out, dst, dst_stride);\
    LSX_ST_8X16(out, out, out, out, out, out, out, out, dst, dst_stride);\
    LSX_ST_8X16(out, out, out, out, out, out, out, out, dst, dst_stride);\
}

INTRA_PREDICT_VALDC_32X32_LSX(127);
INTRA_PREDICT_VALDC_32X32_LSX(128);
INTRA_PREDICT_VALDC_32X32_LSX(129);

void ff_tm_4x4_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                   const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    __m128i tmp0, tmp1, tmp2, tmp3, reg0, reg1;
    __m128i src0, src1, src2, src3;
    __m128i dst0, dst1, dst2, dst3;

    reg0 = __lsx_vreplgr2vr_h(top_left);
    reg1 = __lsx_vld(src_top_ptr, 0);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 0, src_left, 1, src_left, 2, src_left,
              3, tmp3, tmp2, tmp1, tmp0);
    DUP4_ARG2(__lsx_vilvl_b, tmp0, reg1, tmp1, reg1, tmp2, reg1, tmp3, reg1,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vhaddw_hu_bu, src0, src0, src1, src1, src2, src2, src3,
              src3, dst0, dst1, dst2, dst3);
    DUP4_ARG2(__lsx_vssub_hu, dst0, reg0, dst1, reg0, dst2, reg0, dst3, reg0,
              dst0, dst1, dst2, dst3);
    DUP4_ARG2(__lsx_vsat_hu, dst0, 7, dst1, 7, dst2, 7, dst3, 7,
              dst0, dst1, dst2, dst3);
    DUP2_ARG2(__lsx_vpickev_b, dst1, dst0, dst3, dst2, dst0, dst1);
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 2);
}

void ff_tm_8x8_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                   const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i reg0, reg1;

    reg0 = __lsx_vreplgr2vr_h(top_left);
    reg1 = __lsx_vld(src_top_ptr, 0);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 0, src_left, 1, src_left, 2, src_left,
              3, tmp7, tmp6, tmp5, tmp4);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 4, src_left, 5, src_left, 6, src_left,
              7, tmp3, tmp2, tmp1, tmp0);
    DUP4_ARG2(__lsx_vilvl_b, tmp0, reg1, tmp1, reg1, tmp2, reg1, tmp3, reg1,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vilvl_b, tmp4, reg1, tmp5, reg1, tmp6, reg1, tmp7, reg1,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vhaddw_hu_bu, src0, src0, src1, src1, src2, src2, src3,
              src3, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vhaddw_hu_bu, src4, src4, src5, src5, src6, src6, src7,
              src7, src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3, reg0,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7, reg0,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vpickev_b, src1, src0, src3, src2, src5, src4, src7, src6,
              src0, src1, src2, src3);
    __lsx_vstelm_d(src0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(src0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_d(src1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(src1, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_d(src2, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(src2, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_d(src3, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(src3, dst, 0, 1);
}

void ff_tm_16x16_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i reg0, reg1;
    ptrdiff_t stride2 = dst_stride << 1;
    ptrdiff_t stride3 = stride2 + dst_stride;
    ptrdiff_t stride4 = stride2 << 1;

    reg0 = __lsx_vreplgr2vr_h(top_left);
    reg1 = __lsx_vld(src_top_ptr, 0);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 0, src_left, 1, src_left, 2, src_left,
              3, tmp15, tmp14, tmp13, tmp12);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 4, src_left, 5, src_left, 6, src_left,
              7, tmp11, tmp10, tmp9, tmp8);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 8, src_left, 9, src_left, 10,
              src_left, 11, tmp7, tmp6, tmp5, tmp4);
    DUP4_ARG2(__lsx_vldrepl_b, src_left, 12, src_left, 13, src_left, 14,
              src_left, 15, tmp3, tmp2, tmp1, tmp0);
    DUP4_ARG2(__lsx_vaddwev_h_bu, tmp0, reg1, tmp1, reg1, tmp2, reg1, tmp3,
              reg1, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vaddwod_h_bu, tmp0, reg1, tmp1, reg1, tmp2, reg1, tmp3,
              reg1, src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3, reg0,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7, reg0,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vpackev_b, src4, src0, src5, src1, src6, src2, src7, src3,
              tmp0, tmp1, tmp2, tmp3);
    DUP4_ARG2(__lsx_vaddwev_h_bu, tmp4, reg1, tmp5, reg1, tmp6, reg1, tmp7,
              reg1, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vaddwod_h_bu, tmp4, reg1, tmp5, reg1, tmp6, reg1, tmp7,
              reg1, src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3, reg0,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7, reg0,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vpackev_b, src4, src0, src5, src1, src6, src2, src7, src3,
              tmp4, tmp5, tmp6, tmp7);
    DUP4_ARG2(__lsx_vaddwev_h_bu, tmp8, reg1, tmp9, reg1, tmp10, reg1, tmp11,
              reg1, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vaddwod_h_bu, tmp8, reg1, tmp9, reg1, tmp10, reg1, tmp11,
              reg1, src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3, reg0,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7, reg0,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vpackev_b, src4, src0, src5, src1, src6, src2, src7, src3,
              tmp8, tmp9, tmp10, tmp11);
    DUP4_ARG2(__lsx_vaddwev_h_bu, tmp12, reg1, tmp13, reg1, tmp14, reg1,
              tmp15, reg1, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vaddwod_h_bu, tmp12, reg1, tmp13, reg1, tmp14, reg1,
              tmp15, reg1, src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3, reg0,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7, reg0,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
              src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
              src4, src5, src6, src7);
    DUP4_ARG2(__lsx_vpackev_b, src4, src0, src5, src1, src6, src2, src7, src3,
              tmp12, tmp13, tmp14, tmp15);
    LSX_ST_8(tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, dst,
             dst_stride, stride2, stride3, stride4);
    dst += stride4;
    LSX_ST_8(tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, dst,
             dst_stride, stride2, stride3, stride4);
}

void ff_tm_32x32_lsx(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    uint32_t loop_cnt;
    __m128i tmp0, tmp1, tmp2, tmp3, reg0, reg1, reg2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;

    reg0 = __lsx_vreplgr2vr_h(top_left);
    DUP2_ARG2(__lsx_vld, src_top_ptr, 0, src_top_ptr, 16, reg1, reg2);

    src_left += 28;
    for (loop_cnt = 8; loop_cnt--;) {
        DUP4_ARG2(__lsx_vldrepl_b, src_left, 0, src_left, 1, src_left, 2,
                  src_left, 3, tmp3, tmp2, tmp1, tmp0);
        src_left -= 4;
        DUP4_ARG2(__lsx_vaddwev_h_bu, tmp0, reg1, tmp1, reg1, tmp2, reg1,
                  tmp3, reg1, src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vaddwod_h_bu, tmp0, reg1, tmp1, reg1, tmp2, reg1,
                  tmp3, reg1, src4, src5, src6, src7);
        DUP4_ARG2(__lsx_vssub_hu, src0, reg0, src1, reg0, src2, reg0, src3,
                  reg0, src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vssub_hu, src4, reg0, src5, reg0, src6, reg0, src7,
                  reg0, src4, src5, src6, src7);
        DUP4_ARG2(__lsx_vaddwev_h_bu, tmp0, reg2, tmp1, reg2, tmp2, reg2,
                  tmp3, reg2, dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vaddwod_h_bu, tmp0, reg2, tmp1, reg2, tmp2, reg2,
                  tmp3, reg2, dst4, dst5, dst6, dst7);
        DUP4_ARG2(__lsx_vssub_hu, dst0, reg0, dst1, reg0, dst2, reg0, dst3,
                  reg0, dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vssub_hu, dst4, reg0, dst5, reg0, dst6, reg0, dst7,
                  reg0, dst4, dst5, dst6, dst7);
        DUP4_ARG2(__lsx_vsat_hu, src0, 7, src1, 7, src2, 7, src3, 7,
                  src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vsat_hu, src4, 7, src5, 7, src6, 7, src7, 7,
                  src4, src5, src6, src7);
        DUP4_ARG2(__lsx_vsat_hu, dst0, 7, dst1, 7, dst2, 7, dst3, 7,
                  dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vsat_hu, dst4, 7, dst5, 7, dst6, 7, dst7, 7,
                  dst4, dst5, dst6, dst7);
        DUP4_ARG2(__lsx_vpackev_b, src4, src0, src5, src1, src6, src2, src7,
                  src3, src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vpackev_b, dst4, dst0, dst5, dst1, dst6, dst2, dst7,
                  dst3, dst0, dst1, dst2, dst3);
        __lsx_vst(src0, dst, 0);
        __lsx_vst(dst0, dst, 16);
        dst += dst_stride;
        __lsx_vst(src1, dst, 0);
        __lsx_vst(dst1, dst, 16);
        dst += dst_stride;
        __lsx_vst(src2, dst, 0);
        __lsx_vst(dst2, dst, 16);
        dst += dst_stride;
        __lsx_vst(src3, dst, 0);
        __lsx_vst(dst3, dst, 16);
        dst += dst_stride;
    }
}
