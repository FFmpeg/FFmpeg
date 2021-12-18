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

static const uint8_t mc_filt_mask_arr[16 * 3] = {
    /* 8 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
    /* 4 width cases */
    0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20,
    /* 4 width cases */
    8, 9, 9, 10, 10, 11, 11, 12, 24, 25, 25, 26, 26, 27, 27, 28
};


#define HORIZ_8TAP_4WID_4VECS_FILT(_src0, _src1, _src2, _src3,                 \
                                   _mask0, _mask1, _mask2, _mask3,             \
                                   _filter0, _filter1, _filter2, _filter3,     \
                                   _out0, _out1)                               \
{                                                                              \
    __m128i _tmp0, _tmp1, _tmp2, _tmp3, _tmp4, _tmp5, _tmp6, _tmp7;            \
    __m128i _reg0, _reg1, _reg2, _reg3;                                        \
                                                                               \
    DUP2_ARG3(__lsx_vshuf_b, _src1, _src0, _mask0, _src3, _src2, _mask0,       \
              _tmp0, _tmp1);                                                   \
    DUP2_ARG2(__lsx_vdp2_h_b, _tmp0, _filter0, _tmp1, _filter0, _reg0, _reg1); \
    DUP2_ARG3(__lsx_vshuf_b, _src1, _src0, _mask1, _src3, _src2, _mask1,       \
               _tmp2, _tmp3);                                                  \
    DUP2_ARG3(__lsx_vdp2add_h_b, _reg0, _tmp2, _filter1, _reg1, _tmp3,         \
              _filter1, _reg0, _reg1);                                         \
    DUP2_ARG3(__lsx_vshuf_b, _src1, _src0, _mask2, _src3, _src2, _mask2,       \
               _tmp4, _tmp5);                                                  \
    DUP2_ARG2(__lsx_vdp2_h_b, _tmp4, _filter2, _tmp5, _filter2, _reg2, _reg3); \
    DUP2_ARG3(__lsx_vshuf_b, _src1, _src0, _mask3, _src3, _src2, _mask3,       \
               _tmp6, _tmp7);                                                  \
    DUP2_ARG3(__lsx_vdp2add_h_b, _reg2, _tmp6, _filter3, _reg3, _tmp7,         \
              _filter3, _reg2, _reg3);                                         \
    DUP2_ARG2(__lsx_vsadd_h, _reg0, _reg2, _reg1, _reg3, _out0, _out1);        \
}

#define HORIZ_8TAP_8WID_4VECS_FILT(_src0, _src1, _src2, _src3,                 \
                                   _mask0, _mask1, _mask2, _mask3,             \
                                   _filter0, _filter1, _filter2, _filter3,     \
                                   _out0, _out1, _out2, _out3)                 \
{                                                                              \
    __m128i _tmp0, _tmp1, _tmp2, _tmp3, _tmp4, _tmp5, _tmp6, _tmp7;            \
    __m128i _reg0, _reg1, _reg2, _reg3, _reg4, _reg5, _reg6, _reg7;            \
                                                                               \
    DUP4_ARG3(__lsx_vshuf_b, _src0, _src0, _mask0, _src1, _src1, _mask0, _src2,\
              _src2, _mask0, _src3, _src3, _mask0, _tmp0, _tmp1, _tmp2, _tmp3);\
    DUP4_ARG2(__lsx_vdp2_h_b, _tmp0, _filter0, _tmp1, _filter0, _tmp2,         \
              _filter0, _tmp3, _filter0, _reg0, _reg1, _reg2, _reg3);          \
    DUP4_ARG3(__lsx_vshuf_b, _src0, _src0, _mask2, _src1, _src1, _mask2, _src2,\
              _src2, _mask2, _src3, _src3, _mask2, _tmp0, _tmp1, _tmp2, _tmp3);\
    DUP4_ARG2(__lsx_vdp2_h_b, _tmp0, _filter2, _tmp1, _filter2, _tmp2,         \
              _filter2, _tmp3, _filter2, _reg4, _reg5, _reg6, _reg7);          \
    DUP4_ARG3(__lsx_vshuf_b, _src0, _src0, _mask1, _src1, _src1, _mask1, _src2,\
              _src2, _mask1, _src3, _src3, _mask1, _tmp4, _tmp5, _tmp6, _tmp7);\
    DUP4_ARG3(__lsx_vdp2add_h_b, _reg0, _tmp4, _filter1, _reg1, _tmp5,         \
              _filter1, _reg2, _tmp6, _filter1, _reg3, _tmp7, _filter1, _reg0, \
              _reg1, _reg2, _reg3);                                            \
    DUP4_ARG3(__lsx_vshuf_b, _src0, _src0, _mask3, _src1, _src1, _mask3, _src2,\
              _src2, _mask3, _src3, _src3, _mask3, _tmp4, _tmp5, _tmp6, _tmp7);\
    DUP4_ARG3(__lsx_vdp2add_h_b, _reg4, _tmp4, _filter3, _reg5, _tmp5,         \
              _filter3, _reg6, _tmp6, _filter3, _reg7, _tmp7, _filter3, _reg4, \
              _reg5, _reg6, _reg7);                                            \
    DUP4_ARG2(__lsx_vsadd_h, _reg0, _reg4, _reg1, _reg5, _reg2, _reg6, _reg3,  \
              _reg7, _out0, _out1, _out2, _out3);                              \
}

#define FILT_8TAP_DPADD_S_H(_reg0, _reg1, _reg2, _reg3,                        \
                             _filter0, _filter1, _filter2, _filter3)           \
( {                                                                            \
    __m128i _vec0, _vec1;                                                      \
                                                                               \
    _vec0 = __lsx_vdp2_h_b(_reg0, _filter0);                                   \
    _vec0 = __lsx_vdp2add_h_b(_vec0, _reg1, _filter1);                         \
    _vec1 = __lsx_vdp2_h_b(_reg2, _filter2);                                   \
    _vec1 = __lsx_vdp2add_h_b(_vec1, _reg3, _filter3);                         \
    _vec0 = __lsx_vsadd_h(_vec0, _vec1);                                       \
                                                                               \
    _vec0;                                                                     \
} )

#define HORIZ_8TAP_FILT(_src0, _src1, _mask0, _mask1, _mask2, _mask3,          \
                        _filt_h0, _filt_h1, _filt_h2, _filt_h3)                \
( {                                                                            \
    __m128i _tmp0, _tmp1, _tmp2, _tmp3;                                        \
    __m128i _out;                                                              \
                                                                               \
    DUP4_ARG3(__lsx_vshuf_b, _src1, _src0, _mask0, _src1, _src0, _mask1, _src1,\
              _src0, _mask2, _src1, _src0, _mask3, _tmp0, _tmp1, _tmp2, _tmp3);\
    _out = FILT_8TAP_DPADD_S_H(_tmp0, _tmp1, _tmp2, _tmp3, _filt_h0, _filt_h1, \
                               _filt_h2, _filt_h3);                            \
    _out = __lsx_vsrari_h(_out, 7);                                            \
    _out = __lsx_vsat_h(_out, 7);                                              \
                                                                               \
    _out;                                                                      \
} )

#define LSX_LD_4(_src, _stride, _src0, _src1, _src2, _src3)               \
{                                                                         \
    _src0 = __lsx_vld(_src, 0);                                           \
    _src += _stride;                                                      \
    _src1 = __lsx_vld(_src, 0);                                           \
    _src += _stride;                                                      \
    _src2 = __lsx_vld(_src, 0);                                           \
    _src += _stride;                                                      \
    _src3 = __lsx_vld(_src, 0);                                           \
}

static void common_hz_8t_4x4_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out, out0, out1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    src -= 3;
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    LSX_LD_4(src, src_stride, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                     mask3, filter0, filter1, filter2, filter3, out0, out1);
    out = __lsx_vssrarni_b_h(out1, out0, 7);
    out = __lsx_vxori_b(out, 128);
    __lsx_vstelm_w(out, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(out, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(out, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(out, dst, 0, 3);
}

static void common_hz_8t_4x8_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;
    uint8_t *_src = (uint8_t*)src - 3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                     mask3, filter0, filter1, filter2, filter3, out0, out1);
    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
                     mask3, filter0, filter1, filter2, filter3, out2, out3);
    DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
    DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
    __lsx_vstelm_w(out0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(out0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(out0, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(out0, dst, 0, 3);
    dst += dst_stride;
    __lsx_vstelm_w(out1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(out1, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(out1, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(out1, dst, 0, 3);
}

static void common_hz_8t_4w_lsx(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (height == 4) {
        common_hz_8t_4x4_lsx(src, src_stride, dst, dst_stride, filter);
    } else if (height == 8) {
        common_hz_8t_4x8_lsx(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_8t_8x4_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter)
{
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    LSX_LD_4(src, src_stride, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
         mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
    DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
    DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
    __lsx_vstelm_d(out0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(out0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_d(out1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_d(out1, dst, 0, 1);
}

static void common_hz_8t_8x8mult_lsx(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;
    uint8_t* _src = (uint8_t*)src - 3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (; loop_cnt--;) {
        src0 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
        src3 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;
    }
}

static void common_hz_8t_8w_lsx(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    if (height == 4) {
        common_hz_8t_8x4_lsx(src, src_stride, dst, dst_stride, filter);
    } else {
        common_hz_8t_8x8mult_lsx(src, src_stride, dst, dst_stride,
                                 filter, height);
    }
}

static void common_hz_8t_16w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 1;
    int32_t stride = src_stride << 1;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (; loop_cnt--;) {
        const uint8_t* _src = src + src_stride;
        DUP2_ARG2(__lsx_vld, src, 0, _src, 0, src0, src2);
        DUP2_ARG2(__lsx_vld, src, 8, _src, 8, src1, src3);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 0);
        dst += dst_stride;
        __lsx_vst(out1, dst, 0);
        dst += dst_stride;
        src += stride;
    }
}

static void common_hz_8t_32w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 1;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (; loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src2);
        src3 = __lsx_vld(src, 24);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        src += src_stride;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 0);
        __lsx_vst(out1, dst, 16);

        DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src2);
        src3 = __lsx_vld(src, 24);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        src += src_stride;

        dst += dst_stride;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 0);
        __lsx_vst(out1, dst, 16);
        dst += dst_stride;
    }
}

static void common_hz_8t_64w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    int32_t loop_cnt = height;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (; loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src2);
        src3 = __lsx_vld(src, 24);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 0);
        __lsx_vst(out1, dst, 16);

        DUP2_ARG2(__lsx_vld, src, 32, src, 48, src0, src2);
        src3 = __lsx_vld(src, 56);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vst(out0, dst, 32);
        __lsx_vst(out1, dst, 48);
        src += src_stride;
        dst += dst_stride;
    }
}

static void common_vt_8t_4w_lsx(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    __m128i reg0, reg1, reg2, reg3, reg4;
    __m128i filter0, filter1, filter2, filter3;
    __m128i out0, out1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1, tmp0,
              tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, tmp4, tmp5);
    DUP2_ARG2(__lsx_vilvl_d, tmp3, tmp0, tmp4, tmp1, reg0, reg1);
    reg2 = __lsx_vilvl_d(tmp5, tmp2);
    DUP2_ARG2(__lsx_vxori_b, reg0, 128, reg1, 128, reg0, reg1);
    reg2 = __lsx_vxori_b(reg2, 128);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                  src9, tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, reg3, reg4);
        DUP2_ARG2(__lsx_vxori_b, reg3, 128, reg4, 128, reg3, reg4);
        out0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, reg3, filter0, filter1,
                                   filter2, filter3);
        out1 = FILT_8TAP_DPADD_S_H(reg1, reg2, reg3, reg4, filter0, filter1,
                                   filter2, filter3);
        out0 = __lsx_vssrarni_b_h(out1, out0, 7);
        out0 = __lsx_vxori_b(out0, 128);
        __lsx_vstelm_w(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 2);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 3);
        dst += dst_stride;

        reg0 = reg2;
        reg1 = reg3;
        reg2 = reg4;
        src6 = src10;
    }
}

static void common_vt_8t_8w_lsx(const uint8_t *src, int32_t src_stride,
                                uint8_t *dst, int32_t dst_stride,
                                const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5;
    __m128i filter0, filter1, filter2, filter3;
    __m128i out0, out1, out2, out3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
              reg0, reg1, reg2, reg3);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, reg4, reg5);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                  src9, tmp0, tmp1, tmp2, tmp3);
        out0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, tmp0, filter0, filter1,
                                   filter2, filter3);
        out1 = FILT_8TAP_DPADD_S_H(reg3, reg4, reg5, tmp1, filter0, filter1,
                                   filter2, filter3);
        out2 = FILT_8TAP_DPADD_S_H(reg1, reg2, tmp0, tmp2, filter0, filter1,
                                   filter2, filter3);
        out3 = FILT_8TAP_DPADD_S_H(reg4, reg5, tmp1, tmp3, filter0, filter1,
                                   filter2, filter3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;

        reg0 = reg2;
        reg1 = tmp0;
        reg2 = tmp2;
        reg3 = reg5;
        reg4 = tmp1;
        reg5 = tmp3;
        src6 = src10;
    }
}

static void common_vt_8t_16w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filter0, filter1, filter2, filter3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5;
    __m128i reg6, reg7, reg8, reg9, reg10, reg11;
    __m128i tmp0, tmp1, tmp2, tmp3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
              reg0, reg1, reg2, reg3);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, reg4, reg5);
    DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src5, src4, src2, src1,
              reg6, reg7, reg8, reg9);
    DUP2_ARG2(__lsx_vilvh_b, src4, src3, src6, src5, reg10, reg11);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10, src9,
                  src0, src1, src2, src3);
        DUP4_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src9, src8, src10, src9,
                  src4, src5, src7, src8);
        tmp0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, src0, filter0, filter1,
                                   filter2, filter3);
        tmp1 = FILT_8TAP_DPADD_S_H(reg3, reg4, reg5, src1, filter0, filter1,
                                   filter2, filter3);
        tmp2 = FILT_8TAP_DPADD_S_H(reg6, reg7, reg8, src4, filter0, filter1,
                                   filter2, filter3);
        tmp3 = FILT_8TAP_DPADD_S_H(reg9, reg10, reg11, src5, filter0, filter1,
                                   filter2, filter3);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;
        tmp0 = FILT_8TAP_DPADD_S_H(reg1, reg2, src0, src2, filter0, filter1,
                                   filter2, filter3);
        tmp1 = FILT_8TAP_DPADD_S_H(reg4, reg5, src1, src3, filter0, filter1,
                                   filter2, filter3);
        tmp2 = FILT_8TAP_DPADD_S_H(reg7, reg8, src4, src7, filter0, filter1,
                                   filter2, filter3);
        tmp3 = FILT_8TAP_DPADD_S_H(reg10, reg11, src5, src8, filter0, filter1,
                                   filter2, filter3);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        __lsx_vst(tmp0, dst, 0);
        dst += dst_stride;
        __lsx_vst(tmp1, dst, 0);
        dst += dst_stride;

        reg0 = reg2;
        reg1 = src0;
        reg2 = src2;
        reg3 = reg5;
        reg4 = src1;
        reg5 = src3;
        reg6 = reg8;
        reg7 = src4;
        reg8 = src7;
        reg9 = reg11;
        reg10 = src5;
        reg11 = src8;
        src6 = src10;
    }
}

static void common_vt_8t_16w_mult_lsx(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter, int32_t height,
                                      int32_t width)
{
    uint8_t *src_tmp;
    uint8_t *dst_tmp;
    uint32_t cnt = width >> 4;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filter0, filter1, filter2, filter3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5;
    __m128i reg6, reg7, reg8, reg9, reg10, reg11;
    __m128i tmp0, tmp1, tmp2, tmp3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    for (;cnt--;) {
        uint32_t loop_cnt = height >> 2;

        src_tmp = _src;
        dst_tmp = dst;

        src0 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                  src1, src2);
        src3 = __lsx_vldx(src_tmp, src_stride3);
        src_tmp += src_stride4;
        src4 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                  src5, src6);
        src_tmp += src_stride3;

        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
        src6 = __lsx_vxori_b(src6, 128);
        DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  reg0, reg1, reg2, reg3);
        DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, reg4, reg5);
        DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  reg6, reg7, reg8, reg9);
        DUP2_ARG2(__lsx_vilvh_b, src4, src3, src6, src5, reg10, reg11);

        for (;loop_cnt--;) {
            src7 = __lsx_vld(src_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                      src8, src9);
            src10 = __lsx_vldx(src_tmp, src_stride3);
            src_tmp += src_stride4;
            DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10,
                      128, src7, src8, src9, src10);
            DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8,
                      src10, src9, src0, src1, src2, src3);
            DUP4_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src9, src8,
                      src10, src9, src4, src5, src7, src8);
            tmp0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, src0, filter0,
                                       filter1, filter2, filter3);
            tmp1 = FILT_8TAP_DPADD_S_H(reg3, reg4, reg5, src1, filter0,
                                       filter1, filter2, filter3);
            tmp2 = FILT_8TAP_DPADD_S_H(reg6, reg7, reg8, src4, filter0,
                                       filter1, filter2, filter3);
            tmp3 = FILT_8TAP_DPADD_S_H(reg9, reg10, reg11, src5, filter0,
                                       filter1, filter2, filter3);
            DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7,
                      tmp0, tmp1);
            DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
            __lsx_vst(tmp0, dst_tmp, 0);
            __lsx_vstx(tmp1, dst_tmp, dst_stride);
            tmp0 = FILT_8TAP_DPADD_S_H(reg1, reg2, src0, src2, filter0,
                                       filter1, filter2, filter3);
            tmp1 = FILT_8TAP_DPADD_S_H(reg4, reg5, src1, src3, filter0,
                                       filter1, filter2, filter3);
            tmp2 = FILT_8TAP_DPADD_S_H(reg7, reg8, src4, src7, filter0,
                                       filter1, filter2, filter3);
            tmp3 = FILT_8TAP_DPADD_S_H(reg10, reg11, src5, src8, filter0,
                                       filter1, filter2, filter3);
            DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7,
                      tmp0, tmp1);
            DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
            __lsx_vstx(tmp0, dst_tmp, dst_stride2);
            __lsx_vstx(tmp1, dst_tmp, dst_stride3);
            dst_tmp += dst_stride4;

            reg0 = reg2;
            reg1 = src0;
            reg2 = src2;
            reg3 = reg5;
            reg4 = src1;
            reg5 = src3;
            reg6 = reg8;
            reg7 = src4;
            reg8 = src7;
            reg9 = reg11;
            reg10 = src5;
            reg11 = src8;
            src6 = src10;
        }
        _src += 16;
        dst  += 16;
    }
}

static void common_vt_8t_32w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_lsx(src, src_stride, dst, dst_stride, filter, height, 32);
}

static void common_vt_8t_64w_lsx(const uint8_t *src, int32_t src_stride,
                                 uint8_t *dst, int32_t dst_stride,
                                 const int8_t *filter, int32_t height)
{
    common_vt_8t_16w_mult_lsx(src, src_stride, dst, dst_stride,
                              filter, height, 64);
}

static void common_hv_8ht_8vt_4w_lsx(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter_horiz,
                                     const int8_t *filter_vert,
                                     int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    __m128i filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    __m128i out0, out1;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3 - 3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    DUP4_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filter_horiz, 4,
              filter_horiz, 6, filt_hz0, filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);

    tmp0 = HORIZ_8TAP_FILT(src0, src1, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp2 = HORIZ_8TAP_FILT(src2, src3, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp4 = HORIZ_8TAP_FILT(src4, src5, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp5 = HORIZ_8TAP_FILT(src5, src6, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG3(__lsx_vshuf_b, tmp2, tmp0, shuff, tmp4, tmp2, shuff, tmp1, tmp3);
    DUP4_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filter_vert, 4,
              filter_vert, 6, filt_vt0, filt_vt1, filt_vt2, filt_vt3);
    DUP2_ARG2(__lsx_vpackev_b, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    tmp2 = __lsx_vpackev_b(tmp5, tmp4);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        tmp3 = HORIZ_8TAP_FILT(src7, src8, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        tmp4 = __lsx_vshuf_b(tmp3, tmp5, shuff);
        tmp4 = __lsx_vpackev_b(tmp3, tmp4);
        out0 = FILT_8TAP_DPADD_S_H(tmp0, tmp1, tmp2, tmp4, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src1 = HORIZ_8TAP_FILT(src9, src10, mask0, mask1, mask2, mask3,
                               filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        src0 = __lsx_vshuf_b(src1, tmp3, shuff);
        src0 = __lsx_vpackev_b(src1, src0);
        out1 = FILT_8TAP_DPADD_S_H(tmp1, tmp2, tmp4, src0, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        out0 = __lsx_vssrarni_b_h(out1, out0, 7);
        out0 = __lsx_vxori_b(out0, 128);
        __lsx_vstelm_w(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 2);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 3);
        dst += dst_stride;

        tmp5 = src1;
        tmp0 = tmp2;
        tmp1 = tmp4;
        tmp2 = src0;
    }
}

static void common_hv_8ht_8vt_8w_lsx(const uint8_t *src, int32_t src_stride,
                                     uint8_t *dst, int32_t dst_stride,
                                     const int8_t *filter_horiz,
                                     const int8_t *filter_vert,
                                     int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    __m128i filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    __m128i out0, out1;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3 - 3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    DUP4_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filter_horiz,
              4, filter_horiz, 6, filt_hz0, filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);

    src0 = HORIZ_8TAP_FILT(src0, src0, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src1 = HORIZ_8TAP_FILT(src1, src1, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src2 = HORIZ_8TAP_FILT(src2, src2, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src3 = HORIZ_8TAP_FILT(src3, src3, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src4 = HORIZ_8TAP_FILT(src4, src4, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src5 = HORIZ_8TAP_FILT(src5, src5, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src6 = HORIZ_8TAP_FILT(src6, src6, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);

    DUP4_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filter_vert, 4,
              filter_vert, 6, filt_vt0, filt_vt1, filt_vt2, filt_vt3);
    DUP4_ARG2(__lsx_vpackev_b, src1, src0, src3, src2, src5, src4,
              src2, src1, tmp0, tmp1, tmp2, tmp4);
    DUP2_ARG2(__lsx_vpackev_b, src4, src3, src6, src5, tmp5, tmp6);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        src7 = HORIZ_8TAP_FILT(src7, src7, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        tmp3 = __lsx_vpackev_b(src7, src6);
        out0 = FILT_8TAP_DPADD_S_H(tmp0, tmp1, tmp2, tmp3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src8 = HORIZ_8TAP_FILT(src8, src8, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        src0 = __lsx_vpackev_b(src8, src7);
        out1 = FILT_8TAP_DPADD_S_H(tmp4, tmp5, tmp6, src0, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src9 = HORIZ_8TAP_FILT(src9, src9, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        src1 = __lsx_vpackev_b(src9, src8);
        src3 = FILT_8TAP_DPADD_S_H(tmp1, tmp2, tmp3, src1, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src10 = HORIZ_8TAP_FILT(src10, src10, mask0, mask1, mask2, mask3,
                                filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        src2 = __lsx_vpackev_b(src10, src9);
        src4 = FILT_8TAP_DPADD_S_H(tmp5, tmp6, src0, src2, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, src4, src3, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;

        src6 = src10;
        tmp0 = tmp2;
        tmp1 = tmp3;
        tmp2 = src1;
        tmp4 = tmp6;
        tmp5 = src0;
        tmp6 = src2;
    }
}

static void common_hv_8ht_8vt_16w_lsx(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_lsx(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);
        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_32w_lsx(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_lsx(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);
        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_64w_lsx(const uint8_t *src, int32_t src_stride,
                                      uint8_t *dst, int32_t dst_stride,
                                      const int8_t *filter_horiz,
                                      const int8_t *filter_vert,
                                      int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 8; multiple8_cnt--;) {
        common_hv_8ht_8vt_8w_lsx(src, src_stride, dst, dst_stride, filter_horiz,
                                 filter_vert, height);
        src += 8;
        dst += 8;
    }
}

static void copy_width8_lsx(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt = height >> 2;
    __m128i src0, src1, src2, src3;

    for (;cnt--;) {
        src0 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        src1 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        src2 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        src3 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        __lsx_vstelm_d(src0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(src1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(src2, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(src3, dst, 0, 0);
        dst += dst_stride;
    }
}

static void copy_width16_lsx(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt = height >> 2;
    __m128i src0, src1, src2, src3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;
    uint8_t *_src = (uint8_t*)src;

    for (;cnt--;) {
        src0 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
        src3 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        __lsx_vst(src0, dst, 0);
        __lsx_vstx(src1, dst, dst_stride);
        __lsx_vstx(src2, dst, dst_stride2);
        __lsx_vstx(src3, dst, dst_stride3);
        dst += dst_stride4;
    }
}

static void copy_width32_lsx(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt = height >> 2;
    uint8_t *src_tmp1 = (uint8_t*)src;
    uint8_t *dst_tmp1 = dst;
    uint8_t *src_tmp2 = src_tmp1 + 16;
    uint8_t *dst_tmp2 = dst_tmp1 + 16;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;

    for (;cnt--;) {
        src0 = __lsx_vld(src_tmp1, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp1, src_stride, src_tmp1, src_stride2,
                  src1, src2);
        src3 = __lsx_vldx(src_tmp1, src_stride3);
        src_tmp1 += src_stride4;

        src4 = __lsx_vld(src_tmp2, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp2, src_stride, src_tmp2, src_stride2,
                  src5, src6);
        src7 = __lsx_vldx(src_tmp2, src_stride3);
        src_tmp2 += src_stride4;

        __lsx_vst(src0, dst_tmp1, 0);
        __lsx_vstx(src1, dst_tmp1, dst_stride);
        __lsx_vstx(src2, dst_tmp1, dst_stride2);
        __lsx_vstx(src3, dst_tmp1, dst_stride3);
        dst_tmp1 += dst_stride4;
        __lsx_vst(src4, dst_tmp2, 0);
        __lsx_vstx(src5, dst_tmp2, dst_stride);
        __lsx_vstx(src6, dst_tmp2, dst_stride2);
        __lsx_vstx(src7, dst_tmp2, dst_stride3);
        dst_tmp2 += dst_stride4;
    }
}

static void copy_width64_lsx(const uint8_t *src, int32_t src_stride,
                             uint8_t *dst, int32_t dst_stride,
                             int32_t height)
{
    int32_t cnt = height >> 2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i src8, src9, src10, src11, src12, src13, src14, src15;

    for (;cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src0, src1, src2, src3);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src4, src5, src6, src7);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src8, src9, src10, src11);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src12, src13, src14, src15);
        src += src_stride;
        __lsx_vst(src0, dst, 0);
        __lsx_vst(src1, dst, 16);
        __lsx_vst(src2, dst, 32);
        __lsx_vst(src3, dst, 48);
        dst += dst_stride;
        __lsx_vst(src4, dst, 0);
        __lsx_vst(src5, dst, 16);
        __lsx_vst(src6, dst, 32);
        __lsx_vst(src7, dst, 48);
        dst += dst_stride;
        __lsx_vst(src8, dst, 0);
        __lsx_vst(src9, dst, 16);
        __lsx_vst(src10, dst, 32);
        __lsx_vst(src11, dst, 48);
        dst += dst_stride;
        __lsx_vst(src12, dst, 0);
        __lsx_vst(src13, dst, 16);
        __lsx_vst(src14, dst, 32);
        __lsx_vst(src15, dst, 48);
        dst += dst_stride;
    }
}

static void common_hz_8t_and_aver_dst_4x4_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1;
    __m128i dst0, dst1, dst2, dst3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    LSX_LD_4(src, src_stride, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2, mask3,
                               filter0, filter1, filter2, filter3, tmp0, tmp1);
    dst0 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    dst1 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    dst2 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    dst3 = __lsx_vldrepl_w(dst_tmp, 0);
    dst0 = __lsx_vilvl_w(dst1, dst0);
    dst1 = __lsx_vilvl_w(dst3, dst2);
    dst0 = __lsx_vilvl_d(dst1, dst0);
    tmp0 = __lsx_vssrarni_b_h(tmp1, tmp0, 7);
    tmp0 = __lsx_vxori_b(tmp0, 128);
    dst0 = __lsx_vavgr_bu(tmp0, dst0);
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 3);
}

static void common_hz_8t_and_aver_dst_4x8_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter)
{
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3, tmp0, tmp1, tmp2, tmp3;
    __m128i dst0, dst1;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    LSX_LD_4(src, src_stride, src0, src1, src2, src3);
    src += src_stride;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    tmp0 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp1 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp2 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp3 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp0 = __lsx_vilvl_w(tmp1, tmp0);
    tmp1 = __lsx_vilvl_w(tmp3, tmp2);
    dst0 = __lsx_vilvl_d(tmp1, tmp0);

    tmp0 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp1 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp2 = __lsx_vldrepl_w(dst_tmp, 0);
    dst_tmp += dst_stride;
    tmp3 = __lsx_vldrepl_w(dst_tmp, 0);
    tmp0 = __lsx_vilvl_w(tmp1, tmp0);
    tmp1 = __lsx_vilvl_w(tmp3, tmp2);
    dst1 = __lsx_vilvl_d(tmp1, tmp0);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2, mask3,
                               filter0, filter1, filter2, filter3, tmp0, tmp1);
    LSX_LD_4(src, src_stride, src0, src1, src2, src3);
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    HORIZ_8TAP_4WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2, mask3,
                               filter0, filter1, filter2, filter3, tmp2, tmp3);
    DUP4_ARG3(__lsx_vssrarni_b_h, tmp0, tmp0, 7, tmp1, tmp1, 7, tmp2, tmp2, 7,
              tmp3, tmp3, 7, tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
    DUP2_ARG2(__lsx_vavgr_bu, tmp0, dst0, tmp1, dst1, dst0, dst1);
    __lsx_vstelm_w(dst0, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(dst0, dst, 0, 3);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 0);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 1);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 2);
    dst += dst_stride;
    __lsx_vstelm_w(dst1, dst, 0, 3);
}

static void common_hz_8t_and_aver_dst_4w_lsx(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    if (height == 4) {
        common_hz_8t_and_aver_dst_4x4_lsx(src, src_stride, dst, dst_stride, filter);
    } else if (height == 8) {
        common_hz_8t_and_aver_dst_4x8_lsx(src, src_stride, dst, dst_stride, filter);
    }
}

static void common_hz_8t_and_aver_dst_8w_lsx(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    int32_t loop_cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i dst0, dst1, dst2, dst3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride2 + src_stride;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t *_src = (uint8_t*)src - 3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (;loop_cnt--;) {
        src0 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
        src3 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
              mask3,filter0, filter1, filter2, filter3, tmp0, tmp1, tmp2, tmp3);
        dst0 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        dst1 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        dst2 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        dst3 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_d, dst1, dst0, dst3, dst2, dst0, dst1);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        DUP2_ARG2(__lsx_vavgr_bu, tmp0, dst0, tmp1, dst1, dst0, dst1);
        __lsx_vstelm_d(dst0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(dst0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(dst1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(dst1, dst, 0, 1);
        dst += dst_stride;
    }
}

static void common_hz_8t_and_aver_dst_16w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    int32_t loop_cnt = height >> 1;
    int32_t dst_stride2 = dst_stride << 1;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3, dst0, dst1, dst2, dst3;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    for (;loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src, 0, src, 8, src0, src1);
        src += src_stride;
        DUP2_ARG2(__lsx_vld, src, 0, src, 8, src2, src3);
        src += src_stride;
        dst0 = __lsx_vld(dst_tmp, 0);
        dst1 = __lsx_vldx(dst_tmp, dst_stride);
        dst_tmp += dst_stride2;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src1, mask0, src2, src2,
                  mask0, src3, src3, mask0, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src1, mask1, src2, src2,
                  mask1, src3, src3, mask1, tmp4, tmp5, tmp6, tmp7);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask2, src1, src1, mask2, src2, src2,
                  mask2, src3, src3, mask2, tmp8, tmp9, tmp10, tmp11);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask3, src1, src1, mask3, src2, src2,
                  mask3, src3, src3, mask3, tmp12, tmp13, tmp14, tmp15);
        DUP4_ARG2(__lsx_vdp2_h_b, tmp0, filter0, tmp1, filter0, tmp2, filter0, tmp3,
                  filter0, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG2(__lsx_vdp2_h_b, tmp8, filter2, tmp9, filter2, tmp10, filter2, tmp11,
                  filter2, tmp8, tmp9, tmp10, tmp11);
        DUP4_ARG3(__lsx_vdp2add_h_b, tmp0, tmp4, filter1, tmp1, tmp5, filter1, tmp2,
                  tmp6, filter1, tmp3, tmp7, filter1, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG3(__lsx_vdp2add_h_b, tmp8, tmp12, filter3, tmp9, tmp13, filter3, tmp10,
                  tmp14, filter3, tmp11, tmp15, filter3, tmp4, tmp5, tmp6, tmp7);
        DUP4_ARG2(__lsx_vsadd_h, tmp0, tmp4, tmp1, tmp5, tmp2, tmp6, tmp3, tmp7,
                  tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, dst2, dst3);
        DUP2_ARG2(__lsx_vxori_b, dst2, 128, dst3, 128, dst2, dst3);
        DUP2_ARG2(__lsx_vavgr_bu, dst0, dst2, dst1, dst3, dst0, dst1);
        __lsx_vst(dst0, dst, 0);
        __lsx_vstx(dst1, dst, dst_stride);
        dst += dst_stride2;
    }
}

static void common_hz_8t_and_aver_dst_32w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    uint32_t loop_cnt = height;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3, dst0, dst1;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    __m128i tmp8, tmp9, tmp10, tmp11, tmp12, tmp13, tmp14, tmp15;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
                  filter0, filter1, filter2, filter3);

    for (;loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src2);
        src3 = __lsx_vld(src, 24);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        src += src_stride;
        DUP2_ARG2(__lsx_vld, dst_tmp, 0, dst, 16, dst0, dst1);
        dst_tmp += dst_stride;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask0, src1, src1, mask0, src2,
                  src2, mask0, src3, src3, mask0, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask1, src1, src1, mask1, src2,
                  src2, mask1, src3, src3, mask1, tmp4, tmp5, tmp6, tmp7);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask2, src1, src1, mask2, src2,
                  src2, mask2, src3, src3, mask2, tmp8, tmp9, tmp10, tmp11);
        DUP4_ARG3(__lsx_vshuf_b, src0, src0, mask3, src1, src1, mask3, src2,
                  src2, mask3, src3, src3, mask3, tmp12, tmp13, tmp14, tmp15);
        DUP4_ARG2(__lsx_vdp2_h_b, tmp0, filter0, tmp1, filter0, tmp2, filter0,
                  tmp3, filter0, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG2(__lsx_vdp2_h_b, tmp8, filter2, tmp9, filter2, tmp10, filter2,
                  tmp11, filter2, tmp8, tmp9, tmp10, tmp11);
        DUP4_ARG3(__lsx_vdp2add_h_b, tmp0, tmp4, filter1, tmp1, tmp5, filter1,
             tmp2, tmp6, filter1, tmp3, tmp7, filter1, tmp0, tmp1, tmp2, tmp3);
        DUP4_ARG3(__lsx_vdp2add_h_b, tmp8, tmp12, filter3, tmp9, tmp13, filter3,
        tmp10, tmp14, filter3, tmp11, tmp15, filter3, tmp4, tmp5, tmp6, tmp7);
        DUP4_ARG2(__lsx_vsadd_h, tmp0, tmp4, tmp1, tmp5, tmp2, tmp6, tmp3, tmp7,
                  tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG3(__lsx_vssrarni_b_h, tmp1, tmp0, 7, tmp3, tmp2, 7, tmp0, tmp1);
        DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
        DUP2_ARG2(__lsx_vavgr_bu, dst0, tmp0, dst1, tmp1, dst0, dst1);
        __lsx_vst(dst0, dst, 0);
        __lsx_vst(dst1, dst, 16);
        dst += dst_stride;
    }
}

static void common_hz_8t_and_aver_dst_64w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    int32_t loop_cnt = height;
    __m128i src0, src1, src2, src3;
    __m128i filter0, filter1, filter2, filter3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i out0, out1, out2, out3, dst0, dst1;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    src -= 3;
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);
    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
                  filter0, filter1, filter2, filter3);

    for (;loop_cnt--;) {
        DUP2_ARG2(__lsx_vld, src, 0, src, 16, src0, src2);
        src3 = __lsx_vld(src, 24);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        DUP2_ARG2(__lsx_vld, dst, 0, dst, 16, dst0, dst1);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        DUP2_ARG2(__lsx_vavgr_bu, out0, dst0, out1, dst1, out0, out1);
        __lsx_vst(out0, dst, 0);
        __lsx_vst(out1, dst, 16);

        DUP2_ARG2(__lsx_vld, src, 32, src, 48, src0, src2);
        src3 = __lsx_vld(src, 56);
        src1 = __lsx_vshuf_b(src2, src0, shuff);
        DUP2_ARG2(__lsx_vld, dst, 32, dst, 48, dst0, dst1);
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        HORIZ_8TAP_8WID_4VECS_FILT(src0, src1, src2, src3, mask0, mask1, mask2,
             mask3, filter0, filter1, filter2, filter3, out0, out1, out2, out3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        DUP2_ARG2(__lsx_vavgr_bu, out0, dst0, out1, dst1, out0, out1);
        __lsx_vst(out0, dst, 32);
        __lsx_vst(out1, dst, 48);
        src += src_stride;
        dst += dst_stride;
    }
}

static void common_vt_8t_and_aver_dst_4w_lsx(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    __m128i reg0, reg1, reg2, reg3, reg4;
    __m128i filter0, filter1, filter2, filter3;
    __m128i out0, out1;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
              tmp0, tmp1, tmp2, tmp3);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, tmp4, tmp5);
    DUP2_ARG2(__lsx_vilvl_d, tmp3, tmp0, tmp4, tmp1, reg0, reg1);
    reg2 = __lsx_vilvl_d(tmp5, tmp2);
    DUP2_ARG2(__lsx_vxori_b, reg0, 128, reg1, 128, reg0, reg1);
    reg2 = __lsx_vxori_b(reg2, 128);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        src0 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src1 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src2 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src3 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_w, src1, src0, src3, src2, src0, src1);
        src0 = __lsx_vilvl_d(src1, src0);
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                  src9, tmp0, tmp1, tmp2, tmp3);
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, reg3, reg4);
        DUP2_ARG2(__lsx_vxori_b, reg3, 128, reg4, 128, reg3, reg4);
        out0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, reg3, filter0,
                                   filter1, filter2, filter3);
        out1 = FILT_8TAP_DPADD_S_H(reg1, reg2, reg3, reg4, filter0,
                                   filter1, filter2, filter3);
        out0 = __lsx_vssrarni_b_h(out1, out0, 7);
        out0 = __lsx_vxori_b(out0, 128);
        out0 = __lsx_vavgr_bu(out0, src0);
        __lsx_vstelm_w(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 2);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 3);
        dst += dst_stride;
        reg0 = reg2;
        reg1 = reg3;
        reg2 = reg4;
        src6 = src10;
    }
}

static void common_vt_8t_and_aver_dst_8w_lsx(const uint8_t *src,
                                             int32_t src_stride,
                                             uint8_t *dst, int32_t dst_stride,
                                             const int8_t *filter,
                                             int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i tmp0, tmp1, tmp2, tmp3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5;
    __m128i filter0, filter1, filter2, filter3;
    __m128i out0, out1, out2, out3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);
    DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2,
              src1, reg0, reg1, reg2, reg3);
    DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, reg4, reg5);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        src0 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src1 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src2 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src3 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_d, src1, src0, src3, src2, src0, src1);
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8, src10,
                  src9, tmp0, tmp1, tmp2, tmp3);
        out0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, tmp0, filter0,
                                   filter1, filter2, filter3);
        out1 = FILT_8TAP_DPADD_S_H(reg3, reg4, reg5, tmp1, filter0,
                                   filter1, filter2, filter3);
        out2 = FILT_8TAP_DPADD_S_H(reg1, reg2, tmp0, tmp2, filter0,
                                   filter1, filter2, filter3);
        out3 = FILT_8TAP_DPADD_S_H(reg4, reg5, tmp1, tmp3, filter0,
                                   filter1, filter2, filter3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, out3, out2, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        DUP2_ARG2(__lsx_vavgr_bu, out0, src0, out1, src1, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;

        reg0 = reg2;
        reg1 = tmp0;
        reg2 = tmp2;
        reg3 = reg5;
        reg4 = tmp1;
        reg5 = tmp3;
        src6 = src10;
    }
}

static void common_vt_8t_and_aver_dst_16w_mult_lsx(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter,
                                                   int32_t height,
                                                   int32_t width)
{
    uint8_t *src_tmp;
    uint32_t cnt = width >> 4;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filter0, filter1, filter2, filter3;
    __m128i reg0, reg1, reg2, reg3, reg4, reg5;
    __m128i reg6, reg7, reg8, reg9, reg10, reg11;
    __m128i tmp0, tmp1, tmp2, tmp3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;
    uint8_t *_src = (uint8_t*)src - src_stride3;

    DUP4_ARG2(__lsx_vldrepl_h, filter, 0, filter, 2, filter, 4, filter, 6,
              filter0, filter1, filter2, filter3);
    for (;cnt--;) {
        uint32_t loop_cnt = height >> 2;
        uint8_t *dst_reg = dst;

        src_tmp = _src;
        src0 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                  src1, src2);
        src3 = __lsx_vldx(src_tmp, src_stride3);
        src_tmp += src_stride4;
        src4 = __lsx_vld(src_tmp, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                  src5, src6);
        src_tmp += src_stride3;
        DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
                  src0, src1, src2, src3);
        DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
        src6 = __lsx_vxori_b(src6, 128);
        DUP4_ARG2(__lsx_vilvl_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  reg0, reg1, reg2, reg3);
        DUP2_ARG2(__lsx_vilvl_b, src4, src3, src6, src5, reg4, reg5);
        DUP4_ARG2(__lsx_vilvh_b, src1, src0, src3, src2, src5, src4, src2, src1,
                  reg6, reg7, reg8, reg9);
        DUP2_ARG2(__lsx_vilvh_b, src4, src3, src6, src5, reg10, reg11);

        for (;loop_cnt--;) {
            src7 = __lsx_vld(src_tmp, 0);
            DUP2_ARG2(__lsx_vldx, src_tmp, src_stride, src_tmp, src_stride2,
                      src8, src9);
            src10 = __lsx_vldx(src_tmp, src_stride3);
            src_tmp += src_stride4;
            DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10,
                      128, src7, src8, src9, src10);
            DUP4_ARG2(__lsx_vilvl_b, src7, src6, src8, src7, src9, src8,
                      src10, src9, src0, src1, src2, src3);
            DUP4_ARG2(__lsx_vilvh_b, src7, src6, src8, src7, src9, src8,
                      src10, src9, src4, src5, src7, src8);
            tmp0 = FILT_8TAP_DPADD_S_H(reg0, reg1, reg2, src0, filter0,
                                       filter1, filter2, filter3);
            tmp1 = FILT_8TAP_DPADD_S_H(reg3, reg4, reg5, src1, filter0,
                                       filter1, filter2, filter3);
            tmp2 = FILT_8TAP_DPADD_S_H(reg6, reg7, reg8, src4, filter0,
                                       filter1, filter2, filter3);
            tmp3 = FILT_8TAP_DPADD_S_H(reg9, reg10, reg11, src5, filter0,
                                       filter1, filter2, filter3);
            DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7,
                      tmp0, tmp1);
            DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
            tmp2 = __lsx_vld(dst_reg, 0);
            tmp3 = __lsx_vldx(dst_reg, dst_stride);
            DUP2_ARG2(__lsx_vavgr_bu, tmp0, tmp2, tmp1, tmp3, tmp0, tmp1);
            __lsx_vst(tmp0, dst_reg, 0);
            __lsx_vstx(tmp1, dst_reg, dst_stride);
            tmp0 = FILT_8TAP_DPADD_S_H(reg1, reg2, src0, src2, filter0,
                                       filter1, filter2, filter3);
            tmp1 = FILT_8TAP_DPADD_S_H(reg4, reg5, src1, src3, filter0,
                                       filter1, filter2, filter3);
            tmp2 = FILT_8TAP_DPADD_S_H(reg7, reg8, src4, src7, filter0,
                                       filter1, filter2, filter3);
            tmp3 = FILT_8TAP_DPADD_S_H(reg10, reg11, src5, src8, filter0,
                                       filter1, filter2, filter3);
            DUP2_ARG3(__lsx_vssrarni_b_h, tmp2, tmp0, 7, tmp3, tmp1, 7,
                      tmp0, tmp1);
            DUP2_ARG2(__lsx_vxori_b, tmp0, 128, tmp1, 128, tmp0, tmp1);
            tmp2 = __lsx_vldx(dst_reg, dst_stride2);
            tmp3 = __lsx_vldx(dst_reg, dst_stride3);
            DUP2_ARG2(__lsx_vavgr_bu, tmp0, tmp2, tmp1, tmp3, tmp0, tmp1);
            __lsx_vstx(tmp0, dst_reg, dst_stride2);
            __lsx_vstx(tmp1, dst_reg, dst_stride3);
            dst_reg += dst_stride4;

            reg0 = reg2;
            reg1 = src0;
            reg2 = src2;
            reg3 = reg5;
            reg4 = src1;
            reg5 = src3;
            reg6 = reg8;
            reg7 = src4;
            reg8 = src7;
            reg9 = reg11;
            reg10 = src5;
            reg11 = src8;
            src6 = src10;
        }
        _src += 16;
        dst  += 16;
    }
}

static void common_vt_8t_and_aver_dst_16w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_lsx(src, src_stride, dst, dst_stride,
                                           filter, height, 16);
}

static void common_vt_8t_and_aver_dst_32w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_lsx(src, src_stride, dst, dst_stride,
                                           filter, height, 32);
}

static void common_vt_8t_and_aver_dst_64w_lsx(const uint8_t *src,
                                              int32_t src_stride,
                                              uint8_t *dst, int32_t dst_stride,
                                              const int8_t *filter,
                                              int32_t height)
{
    common_vt_8t_and_aver_dst_16w_mult_lsx(src, src_stride, dst, dst_stride,
                                           filter, height, 64);
}

static void common_hv_8ht_8vt_and_aver_dst_4w_lsx(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter_horiz,
                                                  const int8_t *filter_vert,
                                                  int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    __m128i filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5;
    __m128i out0, out1;
    __m128i shuff = {0x0F0E0D0C0B0A0908, 0x1716151413121110};
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - 3 - src_stride3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 16);
    DUP4_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filter_horiz,
              4, filter_horiz, 6, filt_hz0, filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;

    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);

    tmp0 = HORIZ_8TAP_FILT(src0, src1, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp2 = HORIZ_8TAP_FILT(src2, src3, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp4 = HORIZ_8TAP_FILT(src4, src5, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    tmp5 = HORIZ_8TAP_FILT(src5, src6, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG3(__lsx_vshuf_b, tmp2, tmp0, shuff, tmp4, tmp2, shuff, tmp1, tmp3);
    DUP4_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filter_vert, 4,
              filter_vert, 6, filt_vt0, filt_vt1, filt_vt2, filt_vt3);
    DUP2_ARG2(__lsx_vpackev_b, tmp1, tmp0, tmp3, tmp2, tmp0, tmp1);
    tmp2 = __lsx_vpackev_b(tmp5, tmp4);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;
        src2 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src3 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src4 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        src5 = __lsx_vldrepl_w(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_w, src3, src2, src5, src4, src2, src3);
        src2 = __lsx_vilvl_d(src3, src2);
        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        tmp3 = HORIZ_8TAP_FILT(src7, src8, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        tmp4 = __lsx_vshuf_b(tmp3, tmp5, shuff);
        tmp4 = __lsx_vpackev_b(tmp3, tmp4);
        out0 = FILT_8TAP_DPADD_S_H(tmp0, tmp1, tmp2, tmp4, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src1 = HORIZ_8TAP_FILT(src9, src10, mask0, mask1, mask2, mask3,
                               filt_hz0, filt_hz1, filt_hz2, filt_hz3);
        src0 = __lsx_vshuf_b(src1, tmp3, shuff);
        src0 = __lsx_vpackev_b(src1, src0);
        out1 = FILT_8TAP_DPADD_S_H(tmp1, tmp2, tmp4, src0, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        out0 = __lsx_vssrarni_b_h(out1, out0, 7);
        out0 = __lsx_vxori_b(out0, 128);
        out0 = __lsx_vavgr_bu(out0, src2);
        __lsx_vstelm_w(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 2);
        dst += dst_stride;
        __lsx_vstelm_w(out0, dst, 0, 3);
        dst += dst_stride;

        tmp5 = src1;
        tmp0 = tmp2;
        tmp1 = tmp4;
        tmp2 = src0;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_8w_lsx(const uint8_t *src,
                                                  int32_t src_stride,
                                                  uint8_t *dst,
                                                  int32_t dst_stride,
                                                  const int8_t *filter_horiz,
                                                  const int8_t *filter_vert,
                                                  int32_t height)
{
    uint32_t loop_cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    __m128i filt_hz0, filt_hz1, filt_hz2, filt_hz3;
    __m128i filt_vt0, filt_vt1, filt_vt2, filt_vt3;
    __m128i mask0, mask1, mask2, mask3;
    __m128i tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6;
    __m128i out0, out1;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    uint8_t* _src = (uint8_t*)src - 3 - src_stride3;

    mask0 = __lsx_vld(mc_filt_mask_arr, 0);
    DUP4_ARG2(__lsx_vldrepl_h, filter_horiz, 0, filter_horiz, 2, filter_horiz,
              4, filter_horiz, 6, filt_hz0, filt_hz1, filt_hz2, filt_hz3);
    DUP2_ARG2(__lsx_vaddi_bu, mask0, 2, mask0, 4, mask1, mask2);
    mask3 = __lsx_vaddi_bu(mask0, 6);

    src0 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
    src3 = __lsx_vldx(_src, src_stride3);
    _src += src_stride4;
    src4 = __lsx_vld(_src, 0);
    DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src5, src6);
    _src += src_stride3;
    DUP4_ARG2(__lsx_vxori_b, src0, 128, src1, 128, src2, 128, src3, 128,
              src0, src1, src2, src3);
    DUP2_ARG2(__lsx_vxori_b, src4, 128, src5, 128, src4, src5);
    src6 = __lsx_vxori_b(src6, 128);

    src0 = HORIZ_8TAP_FILT(src0, src0, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src1 = HORIZ_8TAP_FILT(src1, src1, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src2 = HORIZ_8TAP_FILT(src2, src2, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src3 = HORIZ_8TAP_FILT(src3, src3, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src4 = HORIZ_8TAP_FILT(src4, src4, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src5 = HORIZ_8TAP_FILT(src5, src5, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);
    src6 = HORIZ_8TAP_FILT(src6, src6, mask0, mask1, mask2, mask3, filt_hz0,
                           filt_hz1, filt_hz2, filt_hz3);

    DUP4_ARG2(__lsx_vldrepl_h, filter_vert, 0, filter_vert, 2, filter_vert, 4,
              filter_vert, 6, filt_vt0, filt_vt1, filt_vt2, filt_vt3);
    DUP4_ARG2(__lsx_vpackev_b, src1, src0, src3, src2, src5, src4,
              src2, src1, tmp0, tmp1, tmp2, tmp4);
    DUP2_ARG2(__lsx_vpackev_b, src4, src3, src6, src5, tmp5, tmp6);

    for (;loop_cnt--;) {
        src7 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src8, src9);
        src10 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;

        DUP4_ARG2(__lsx_vxori_b, src7, 128, src8, 128, src9, 128, src10, 128,
                  src7, src8, src9, src10);
        src7 = HORIZ_8TAP_FILT(src7, src7, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        tmp3 = __lsx_vpackev_b(src7, src6);
        out0 = FILT_8TAP_DPADD_S_H(tmp0, tmp1, tmp2, tmp3, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src8 = HORIZ_8TAP_FILT(src8, src8, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        src0 = __lsx_vpackev_b(src8, src7);
        out1 = FILT_8TAP_DPADD_S_H(tmp4, tmp5, tmp6, src0, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src9 = HORIZ_8TAP_FILT(src9, src9, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        src1 = __lsx_vpackev_b(src9, src8);
        src3 = FILT_8TAP_DPADD_S_H(tmp1, tmp2, tmp3, src1, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        src10 = HORIZ_8TAP_FILT(src10, src10, mask0, mask1, mask2, mask3, filt_hz0,
                               filt_hz1, filt_hz2, filt_hz3);
        src2 = __lsx_vpackev_b(src10, src9);
        src4 = FILT_8TAP_DPADD_S_H(tmp5, tmp6, src0, src2, filt_vt0, filt_vt1,
                                   filt_vt2, filt_vt3);
        DUP2_ARG3(__lsx_vssrarni_b_h, out1, out0, 7, src4, src3, 7, out0, out1);
        DUP2_ARG2(__lsx_vxori_b, out0, 128, out1, 128, out0, out1);
        src5 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src7 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src8 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        src9 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_d, src7, src5, src9, src8, src5, src7);
        DUP2_ARG2(__lsx_vavgr_bu, out0, src5, out1, src7, out0, out1);
        __lsx_vstelm_d(out0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(out1, dst, 0, 1);
        dst += dst_stride;

        src6 = src10;
        tmp0 = tmp2;
        tmp1 = tmp3;
        tmp2 = src1;
        tmp4 = tmp6;
        tmp5 = src0;
        tmp6 = src2;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_16w_lsx(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 2; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_lsx(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_32w_lsx(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 4; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_lsx(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void common_hv_8ht_8vt_and_aver_dst_64w_lsx(const uint8_t *src,
                                                   int32_t src_stride,
                                                   uint8_t *dst,
                                                   int32_t dst_stride,
                                                   const int8_t *filter_horiz,
                                                   const int8_t *filter_vert,
                                                   int32_t height)
{
    int32_t multiple8_cnt;

    for (multiple8_cnt = 8; multiple8_cnt--;) {
        common_hv_8ht_8vt_and_aver_dst_8w_lsx(src, src_stride, dst, dst_stride,
                                              filter_horiz, filter_vert,
                                              height);

        src += 8;
        dst += 8;
    }
}

static void avg_width8_lsx(const uint8_t *src, int32_t src_stride,
                           uint8_t *dst, int32_t dst_stride,
                           int32_t height)
{
    int32_t cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, dst0, dst1;
    __m128i tmp0, tmp1, tmp2, tmp3;

    for (;cnt--;) {
        tmp0 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        tmp1 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        tmp2 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        tmp3 = __lsx_vldrepl_d(src, 0);
        src += src_stride;
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, src0, src1);
        tmp0 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        tmp1 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        tmp2 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        tmp3 = __lsx_vldrepl_d(dst_tmp, 0);
        dst_tmp += dst_stride;
        DUP2_ARG2(__lsx_vilvl_d, tmp1, tmp0, tmp3, tmp2, dst0, dst1);
        DUP2_ARG2(__lsx_vavgr_bu, src0, dst0, src1, dst1, dst0, dst1);
        __lsx_vstelm_d(dst0, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(dst0, dst, 0, 1);
        dst += dst_stride;
        __lsx_vstelm_d(dst1, dst, 0, 0);
        dst += dst_stride;
        __lsx_vstelm_d(dst1, dst, 0, 1);
        dst += dst_stride;
    }
}

static void avg_width16_lsx(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt = height >> 2;
    __m128i src0, src1, src2, src3;
    __m128i dst0, dst1, dst2, dst3;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;
    uint8_t* _src = (uint8_t*)src;

    for (;cnt--;) {
        src0 = __lsx_vld(_src, 0);
        DUP2_ARG2(__lsx_vldx, _src, src_stride, _src, src_stride2, src1, src2);
        src3 = __lsx_vldx(_src, src_stride3);
        _src += src_stride4;

        dst0 = __lsx_vld(dst, 0);
        DUP2_ARG2(__lsx_vldx, dst, dst_stride, dst, dst_stride2,
                  dst1, dst2);
        dst3 = __lsx_vldx(dst, dst_stride3);
        DUP4_ARG2(__lsx_vavgr_bu, src0, dst0, src1, dst1,
                  src2, dst2, src3, dst3, dst0, dst1, dst2, dst3);
        __lsx_vst(dst0, dst, 0);
        __lsx_vstx(dst1, dst, dst_stride);
        __lsx_vstx(dst2, dst, dst_stride2);
        __lsx_vstx(dst3, dst, dst_stride3);
        dst += dst_stride4;
    }
}

static void avg_width32_lsx(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt = height >> 2;
    uint8_t *src_tmp1 = (uint8_t*)src;
    uint8_t *src_tmp2 = src_tmp1 + 16;
    uint8_t *dst_tmp1, *dst_tmp2;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    int32_t src_stride2 = src_stride << 1;
    int32_t src_stride3 = src_stride + src_stride2;
    int32_t src_stride4 = src_stride2 << 1;
    int32_t dst_stride2 = dst_stride << 1;
    int32_t dst_stride3 = dst_stride2 + dst_stride;
    int32_t dst_stride4 = dst_stride2 << 1;

    dst_tmp1 = dst;
    dst_tmp2 = dst + 16;
    for (;cnt--;) {
        src0 = __lsx_vld(src_tmp1, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp1, src_stride, src_tmp1, src_stride2,
                  src2, src4);
        src6 = __lsx_vldx(src_tmp1, src_stride3);
        src_tmp1 += src_stride4;

        src1 = __lsx_vld(src_tmp2, 0);
        DUP2_ARG2(__lsx_vldx, src_tmp2, src_stride, src_tmp2, src_stride2,
                  src3, src5);
        src7 = __lsx_vldx(src_tmp2, src_stride3);
        src_tmp2 += src_stride4;

        dst0 = __lsx_vld(dst_tmp1, 0);
        DUP2_ARG2(__lsx_vldx, dst_tmp1, dst_stride, dst_tmp1, dst_stride2,
                  dst2, dst4);
        dst6 = __lsx_vldx(dst_tmp1, dst_stride3);
        dst1 = __lsx_vld(dst_tmp2, 0);
        DUP2_ARG2(__lsx_vldx, dst_tmp2, dst_stride, dst_tmp2, dst_stride2,
                  dst3, dst5);
        dst7 = __lsx_vldx(dst_tmp2, dst_stride3);

        DUP4_ARG2(__lsx_vavgr_bu, src0, dst0, src1, dst1,
                  src2, dst2, src3, dst3, dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vavgr_bu, src4, dst4, src5, dst5,
                  src6, dst6, src7, dst7, dst4, dst5, dst6, dst7);
        __lsx_vst(dst0, dst_tmp1, 0);
        __lsx_vstx(dst2, dst_tmp1, dst_stride);
        __lsx_vstx(dst4, dst_tmp1, dst_stride2);
        __lsx_vstx(dst6, dst_tmp1, dst_stride3);
        dst_tmp1 += dst_stride4;
        __lsx_vst(dst1, dst_tmp2, 0);
        __lsx_vstx(dst3, dst_tmp2, dst_stride);
        __lsx_vstx(dst5, dst_tmp2, dst_stride2);
        __lsx_vstx(dst7, dst_tmp2, dst_stride3);
        dst_tmp2 += dst_stride4;
    }
}

static void avg_width64_lsx(const uint8_t *src, int32_t src_stride,
                            uint8_t *dst, int32_t dst_stride,
                            int32_t height)
{
    int32_t cnt = height >> 2;
    uint8_t *dst_tmp = dst;
    __m128i src0, src1, src2, src3, src4, src5, src6, src7;
    __m128i src8, src9, src10, src11, src12, src13, src14, src15;
    __m128i dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    __m128i dst8, dst9, dst10, dst11, dst12, dst13, dst14, dst15;

    for (;cnt--;) {
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src0, src1, src2, src3);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src4, src5, src6, src7);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src8, src9, src10, src11);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, src, 0, src, 16, src, 32, src, 48,
                  src12, src13, src14, src15);
        src += src_stride;
        DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32, dst_tmp, 48,
                  dst0, dst1, dst2, dst3);
        dst_tmp += dst_stride;
        DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32, dst_tmp, 48,
                  dst4, dst5, dst6, dst7);
        dst_tmp += dst_stride;
        DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32, dst_tmp, 48,
                  dst8, dst9, dst10, dst11);
        dst_tmp += dst_stride;
        DUP4_ARG2(__lsx_vld, dst_tmp, 0, dst_tmp, 16, dst_tmp, 32, dst_tmp, 48,
                  dst12, dst13, dst14, dst15);
        dst_tmp += dst_stride;
        DUP4_ARG2(__lsx_vavgr_bu, src0, dst0, src1, dst1,
                  src2, dst2, src3, dst3, dst0, dst1, dst2, dst3);
        DUP4_ARG2(__lsx_vavgr_bu, src4, dst4, src5, dst5,
                  src6, dst6, src7, dst7, dst4, dst5, dst6, dst7);
        DUP4_ARG2(__lsx_vavgr_bu, src8, dst8, src9, dst9, src10,
                  dst10, src11, dst11, dst8, dst9, dst10, dst11);
        DUP4_ARG2(__lsx_vavgr_bu, src12, dst12, src13, dst13, src14,
                  dst14, src15, dst15, dst12, dst13, dst14, dst15);
        __lsx_vst(dst0, dst, 0);
        __lsx_vst(dst1, dst, 16);
        __lsx_vst(dst2, dst, 32);
        __lsx_vst(dst3, dst, 48);
        dst += dst_stride;
        __lsx_vst(dst4, dst, 0);
        __lsx_vst(dst5, dst, 16);
        __lsx_vst(dst6, dst, 32);
        __lsx_vst(dst7, dst, 48);
        dst += dst_stride;
        __lsx_vst(dst8, dst, 0);
        __lsx_vst(dst9, dst, 16);
        __lsx_vst(dst10, dst, 32);
        __lsx_vst(dst11, dst, 48);
        dst += dst_stride;
        __lsx_vst(dst12, dst, 0);
        __lsx_vst(dst13, dst, 16);
        __lsx_vst(dst14, dst, 32);
        __lsx_vst(dst15, dst, 48);
        dst += dst_stride;
    }
}

static const int8_t vp9_subpel_filters_lsx[3][15][8] = {
    [FILTER_8TAP_REGULAR] = {
         {0, 1, -5, 126, 8, -3, 1, 0},
         {-1, 3, -10, 122, 18, -6, 2, 0},
         {-1, 4, -13, 118, 27, -9, 3, -1},
         {-1, 4, -16, 112, 37, -11, 4, -1},
         {-1, 5, -18, 105, 48, -14, 4, -1},
         {-1, 5, -19, 97, 58, -16, 5, -1},
         {-1, 6, -19, 88, 68, -18, 5, -1},
         {-1, 6, -19, 78, 78, -19, 6, -1},
         {-1, 5, -18, 68, 88, -19, 6, -1},
         {-1, 5, -16, 58, 97, -19, 5, -1},
         {-1, 4, -14, 48, 105, -18, 5, -1},
         {-1, 4, -11, 37, 112, -16, 4, -1},
         {-1, 3, -9, 27, 118, -13, 4, -1},
         {0, 2, -6, 18, 122, -10, 3, -1},
         {0, 1, -3, 8, 126, -5, 1, 0},
    }, [FILTER_8TAP_SHARP] = {
        {-1, 3, -7, 127, 8, -3, 1, 0},
        {-2, 5, -13, 125, 17, -6, 3, -1},
        {-3, 7, -17, 121, 27, -10, 5, -2},
        {-4, 9, -20, 115, 37, -13, 6, -2},
        {-4, 10, -23, 108, 48, -16, 8, -3},
        {-4, 10, -24, 100, 59, -19, 9, -3},
        {-4, 11, -24, 90, 70, -21, 10, -4},
        {-4, 11, -23, 80, 80, -23, 11, -4},
        {-4, 10, -21, 70, 90, -24, 11, -4},
        {-3, 9, -19, 59, 100, -24, 10, -4},
        {-3, 8, -16, 48, 108, -23, 10, -4},
        {-2, 6, -13, 37, 115, -20, 9, -4},
        {-2, 5, -10, 27, 121, -17, 7, -3},
        {-1, 3, -6, 17, 125, -13, 5, -2},
        {0, 1, -3, 8, 127, -7, 3, -1},
    }, [FILTER_8TAP_SMOOTH] = {
        {-3, -1, 32, 64, 38, 1, -3, 0},
        {-2, -2, 29, 63, 41, 2, -3, 0},
        {-2, -2, 26, 63, 43, 4, -4, 0},
        {-2, -3, 24, 62, 46, 5, -4, 0},
        {-2, -3, 21, 60, 49, 7, -4, 0},
        {-1, -4, 18, 59, 51, 9, -4, 0},
        {-1, -4, 16, 57, 53, 12, -4, -1},
        {-1, -4, 14, 55, 55, 14, -4, -1},
        {-1, -4, 12, 53, 57, 16, -4, -1},
        {0, -4, 9, 51, 59, 18, -4, -1},
        {0, -4, 7, 49, 60, 21, -3, -2},
        {0, -4, 5, 46, 62, 24, -3, -2},
        {0, -4, 4, 43, 63, 26, -2, -2},
        {0, -3, 2, 41, 63, 29, -2, -2},
        {0, -3, 1, 38, 64, 32, -1, -3},
    }
};

#define VP9_8TAP_LOONGARCH_LSX_FUNC(SIZE, type, type_idx)                      \
void ff_put_8tap_##type##_##SIZE##h_lsx(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_lsx[type_idx][mx-1];             \
                                                                               \
    common_hz_8t_##SIZE##w_lsx(src, srcstride, dst, dststride, filter, h);     \
}                                                                              \
                                                                               \
void ff_put_8tap_##type##_##SIZE##v_lsx(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_lsx[type_idx][my-1];             \
                                                                               \
    common_vt_8t_##SIZE##w_lsx(src, srcstride, dst, dststride, filter, h);     \
}                                                                              \
                                                                               \
void ff_put_8tap_##type##_##SIZE##hv_lsx(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const int8_t *hfilter = vp9_subpel_filters_lsx[type_idx][mx-1];            \
    const int8_t *vfilter = vp9_subpel_filters_lsx[type_idx][my-1];            \
                                                                               \
    common_hv_8ht_8vt_##SIZE##w_lsx(src, srcstride, dst, dststride, hfilter,   \
                                    vfilter, h);                               \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##h_lsx(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_lsx[type_idx][mx-1];             \
                                                                               \
    common_hz_8t_and_aver_dst_##SIZE##w_lsx(src, srcstride, dst,               \
                                            dststride, filter, h);             \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##v_lsx(uint8_t *dst, ptrdiff_t dststride,     \
                                        const uint8_t *src,                    \
                                        ptrdiff_t srcstride,                   \
                                        int h, int mx, int my)                 \
{                                                                              \
    const int8_t *filter = vp9_subpel_filters_lsx[type_idx][my-1];             \
                                                                               \
    common_vt_8t_and_aver_dst_##SIZE##w_lsx(src, srcstride, dst, dststride,    \
                                            filter, h);                        \
}                                                                              \
                                                                               \
void ff_avg_8tap_##type##_##SIZE##hv_lsx(uint8_t *dst, ptrdiff_t dststride,    \
                                         const uint8_t *src,                   \
                                         ptrdiff_t srcstride,                  \
                                         int h, int mx, int my)                \
{                                                                              \
    const int8_t *hfilter = vp9_subpel_filters_lsx[type_idx][mx-1];            \
    const int8_t *vfilter = vp9_subpel_filters_lsx[type_idx][my-1];            \
                                                                               \
    common_hv_8ht_8vt_and_aver_dst_##SIZE##w_lsx(src, srcstride, dst,          \
                                                 dststride, hfilter,           \
                                                 vfilter, h);                  \
}

#define VP9_COPY_LOONGARCH_LSX_FUNC(SIZE)                          \
void ff_copy##SIZE##_lsx(uint8_t *dst, ptrdiff_t dststride,        \
                         const uint8_t *src, ptrdiff_t srcstride,  \
                         int h, int mx, int my)                    \
{                                                                  \
                                                                   \
    copy_width##SIZE##_lsx(src, srcstride, dst, dststride, h);     \
}                                                                  \
void ff_avg##SIZE##_lsx(uint8_t *dst, ptrdiff_t dststride,         \
                        const uint8_t *src, ptrdiff_t srcstride,   \
                        int h, int mx, int my)                     \
{                                                                  \
                                                                   \
    avg_width##SIZE##_lsx(src, srcstride, dst, dststride, h);      \
}

VP9_8TAP_LOONGARCH_LSX_FUNC(64, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, regular, FILTER_8TAP_REGULAR);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, regular, FILTER_8TAP_REGULAR);

VP9_8TAP_LOONGARCH_LSX_FUNC(64, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, sharp, FILTER_8TAP_SHARP);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, sharp, FILTER_8TAP_SHARP);

VP9_8TAP_LOONGARCH_LSX_FUNC(64, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(32, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(16, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(8, smooth, FILTER_8TAP_SMOOTH);
VP9_8TAP_LOONGARCH_LSX_FUNC(4, smooth, FILTER_8TAP_SMOOTH);

VP9_COPY_LOONGARCH_LSX_FUNC(64);
VP9_COPY_LOONGARCH_LSX_FUNC(32);
VP9_COPY_LOONGARCH_LSX_FUNC(16);
VP9_COPY_LOONGARCH_LSX_FUNC(8);

#undef VP9_8TAP_LOONGARCH_LSX_FUNC
#undef VP9_COPY_LOONGARCH_LSX_FUNC
