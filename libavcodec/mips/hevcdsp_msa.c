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
#include "libavcodec/mips/hevcdsp_mips.h"

#define HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,                         \
                               filt0, filt1, filt2, filt3,                     \
                               var_in)                                         \
( {                                                                            \
    v8i16 out;                                                                 \
                                                                               \
    out = __msa_dpadd_s_h((v8i16) (var_in), (v16i8) (vec0), (v16i8) (filt0));  \
    out = __msa_dpadd_s_h(out, (v16i8) (vec1), (v16i8) (filt1));               \
    out = __msa_dpadd_s_h(out, (v16i8) (vec2), (v16i8) (filt2));               \
    out = __msa_dpadd_s_h(out, (v16i8) (vec3), (v16i8) (filt3));               \
    out;                                                                       \
} )

static void hevc_hz_8t_4w_msa(uint8_t * __restrict src, int32_t src_stride,
                              int16_t * __restrict dst, int32_t dst_stride,
                              const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    uint64_t out0, out1, out2, out3, out4, out5, out6, out7;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v8u16 const_vec;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 16, 17, 17, 18, 18, 19, 19, 20 };

    src -= 3;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LOAD_8VECS_SB(src, src_stride,
                      src0, src1, src2, src3, src4, src5, src6, src7);
        src += (8 * src_stride);

        XORI_B_8VECS_SB(src0, src1, src2, src3, src4, src5, src6, src7,
                        src0, src1, src2, src3, src4, src5, src6, src7, 128);

        vec0 = __msa_vshf_b(mask0, src1, src0);
        vec1 = __msa_vshf_b(mask1, src1, src0);
        vec2 = __msa_vshf_b(mask2, src1, src0);
        vec3 = __msa_vshf_b(mask3, src1, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src3, src2);
        vec1 = __msa_vshf_b(mask1, src3, src2);
        vec2 = __msa_vshf_b(mask2, src3, src2);
        vec3 = __msa_vshf_b(mask3, src3, src2);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src5, src4);
        vec1 = __msa_vshf_b(mask1, src5, src4);
        vec2 = __msa_vshf_b(mask2, src5, src4);
        vec3 = __msa_vshf_b(mask3, src5, src4);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src7, src6);
        vec1 = __msa_vshf_b(mask1, src7, src6);
        vec2 = __msa_vshf_b(mask2, src7, src6);
        vec3 = __msa_vshf_b(mask3, src7, src6);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        out0 = __msa_copy_u_d((v2i64) dst0, 0);
        out1 = __msa_copy_u_d((v2i64) dst0, 1);
        out2 = __msa_copy_u_d((v2i64) dst1, 0);
        out3 = __msa_copy_u_d((v2i64) dst1, 1);
        out4 = __msa_copy_u_d((v2i64) dst2, 0);
        out5 = __msa_copy_u_d((v2i64) dst2, 1);
        out6 = __msa_copy_u_d((v2i64) dst3, 0);
        out7 = __msa_copy_u_d((v2i64) dst3, 1);

        STORE_DWORD(dst, out0);
        dst += dst_stride;
        STORE_DWORD(dst, out1);
        dst += dst_stride;
        STORE_DWORD(dst, out2);
        dst += dst_stride;
        STORE_DWORD(dst, out3);
        dst += dst_stride;
        STORE_DWORD(dst, out4);
        dst += dst_stride;
        STORE_DWORD(dst, out5);
        dst += dst_stride;
        STORE_DWORD(dst, out6);
        dst += dst_stride;
        STORE_DWORD(dst, out7);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_8w_msa(uint8_t * __restrict src, int32_t src_stride,
                              int16_t * __restrict dst, int32_t dst_stride,
                              const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v8u16 const_vec;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LOAD_4VECS_SB(src, src_stride, src0, src1, src2, src3);
        src += (4 * src_stride);

        XORI_B_4VECS_SB(src0, src1, src2, src3, src0, src1, src2, src3, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src3, src3);
        vec1 = __msa_vshf_b(mask1, src3, src3);
        vec2 = __msa_vshf_b(mask2, src3, src3);
        vec3 = __msa_vshf_b(mask3, src3, src3);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);
        dst += dst_stride;
        STORE_SH(dst1, dst);
        dst += dst_stride;
        STORE_SH(dst2, dst);
        dst += dst_stride;
        STORE_SH(dst3, dst);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_12w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_hz_8t_8w_msa(src, src_stride, dst, dst_stride, filter, height);

    hevc_hz_8t_4w_msa(src + 8, src_stride, dst + 8, dst_stride, filter, height);
}

static void hevc_hz_8t_16w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3;
    v8u16 const_vec;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LOAD_4VECS_SB(src, src_stride, src0, src2, src4, src6);
        LOAD_4VECS_SB(src + 8, src_stride, src1, src3, src5, src7);
        src += (4 * src_stride);

        XORI_B_8VECS_SB(src0, src1, src2, src3, src4, src5, src6, src7,
                        src0, src1, src2, src3, src4, src5, src6, src7, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src3, src3);
        vec1 = __msa_vshf_b(mask1, src3, src3);
        vec2 = __msa_vshf_b(mask2, src3, src3);
        vec3 = __msa_vshf_b(mask3, src3, src3);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src4, src4);
        vec1 = __msa_vshf_b(mask1, src4, src4);
        vec2 = __msa_vshf_b(mask2, src4, src4);
        vec3 = __msa_vshf_b(mask3, src4, src4);

        dst4 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src5, src5);
        vec1 = __msa_vshf_b(mask1, src5, src5);
        vec2 = __msa_vshf_b(mask2, src5, src5);
        vec3 = __msa_vshf_b(mask3, src5, src5);

        dst5 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src6, src6);
        vec1 = __msa_vshf_b(mask1, src6, src6);
        vec2 = __msa_vshf_b(mask2, src6, src6);
        vec3 = __msa_vshf_b(mask3, src6, src6);

        dst6 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src7, src7);
        vec1 = __msa_vshf_b(mask1, src7, src7);
        vec2 = __msa_vshf_b(mask2, src7, src7);
        vec3 = __msa_vshf_b(mask3, src7, src7);

        dst7 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);
        STORE_SH(dst1, dst + 8);
        dst += dst_stride;
        STORE_SH(dst2, dst);
        STORE_SH(dst3, dst + 8);
        dst += dst_stride;
        STORE_SH(dst4, dst);
        STORE_SH(dst5, dst + 8);
        dst += dst_stride;
        STORE_SH(dst6, dst);
        STORE_SH(dst7, dst + 8);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_24w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec;
    v8u16 const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = (height >> 1); loop_cnt--;) {
        src0 = LOAD_SB(src);
        src1 = LOAD_SB(src + 16);
        src += src_stride;
        src2 = LOAD_SB(src);
        src3 = LOAD_SB(src + 16);
        src += src_stride;

        XORI_B_4VECS_SB(src0, src1, src2, src3, src0, src1, src2, src3, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask4, src1, src0);
        vec1 = __msa_vshf_b(mask5, src1, src0);
        vec2 = __msa_vshf_b(mask6, src1, src0);
        vec3 = __msa_vshf_b(mask7, src1, src0);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask4, src3, src2);
        vec1 = __msa_vshf_b(mask5, src3, src2);
        vec2 = __msa_vshf_b(mask6, src3, src2);
        vec3 = __msa_vshf_b(mask7, src3, src2);

        dst4 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src3, src3);
        vec1 = __msa_vshf_b(mask1, src3, src3);
        vec2 = __msa_vshf_b(mask2, src3, src3);
        vec3 = __msa_vshf_b(mask3, src3, src3);

        dst5 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);
        STORE_SH(dst1, dst + 8);
        STORE_SH(dst2, dst + 16);
        dst += dst_stride;
        STORE_SH(dst3, dst);
        STORE_SH(dst4, dst + 8);
        STORE_SH(dst5, dst + 16);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_32w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3;
    v8i16 filter_vec;
    v8u16 const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LOAD_SB(src);
        src1 = LOAD_SB(src + 16);
        src2 = LOAD_SB(src + 24);
        src += src_stride;

        XORI_B_3VECS_SB(src0, src1, src2, src0, src1, src2, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask4, src1, src0);
        vec1 = __msa_vshf_b(mask5, src1, src0);
        vec2 = __msa_vshf_b(mask6, src1, src0);
        vec3 = __msa_vshf_b(mask7, src1, src0);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);
        STORE_SH(dst1, dst + 8);
        STORE_SH(dst2, dst + 16);
        STORE_SH(dst3, dst + 24);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_48w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5;
    v8i16 filter_vec;
    v8u16 const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LOAD_SB(src);
        src1 = LOAD_SB(src + 16);
        src2 = LOAD_SB(src + 32);
        src3 = LOAD_SB(src + 40);
        src += src_stride;

        XORI_B_4VECS_SB(src0, src1, src2, src3, src0, src1, src2, src3, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask4, src1, src0);
        vec1 = __msa_vshf_b(mask5, src1, src0);
        vec2 = __msa_vshf_b(mask6, src1, src0);
        vec3 = __msa_vshf_b(mask7, src1, src0);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask4, src2, src1);
        vec1 = __msa_vshf_b(mask5, src2, src1);
        vec2 = __msa_vshf_b(mask6, src2, src1);
        vec3 = __msa_vshf_b(mask7, src2, src1);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst4 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        vec0 = __msa_vshf_b(mask0, src3, src3);
        vec1 = __msa_vshf_b(mask1, src3, src3);
        vec2 = __msa_vshf_b(mask2, src3, src3);
        vec3 = __msa_vshf_b(mask3, src3, src3);

        dst5 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);
        STORE_SH(dst1, dst + 8);
        STORE_SH(dst2, dst + 16);
        STORE_SH(dst3, dst + 24);
        STORE_SH(dst4, dst + 32);
        STORE_SH(dst5, dst + 40);
        dst += dst_stride;
    }
}

static void hevc_hz_8t_64w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    uint32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4;
    v8i16 filt0, filt1, filt2, filt3;
    v16i8 mask1, mask2, mask3, mask4, mask5, mask6, mask7;
    v16i8 vec0, vec1, vec2, vec3;
    v8i16 dst0, dst1, dst2, dst3, dst4, dst5, dst6, dst7;
    v8i16 filter_vec;
    v8u16 const_vec;
    v16i8 mask0 = { 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8 };

    src -= 3;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    mask1 = mask0 + 2;
    mask2 = mask0 + 4;
    mask3 = mask0 + 6;
    mask4 = mask0 + 8;
    mask5 = mask0 + 10;
    mask6 = mask0 + 12;
    mask7 = mask0 + 14;

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    for (loop_cnt = height; loop_cnt--;) {
        src0 = LOAD_SB(src);
        src1 = LOAD_SB(src + 16);
        src2 = LOAD_SB(src + 32);
        src3 = LOAD_SB(src + 48);
        src4 = LOAD_SB(src + 56);
        src += src_stride;

        XORI_B_5VECS_SB(src0, src1, src2, src3, src4,
                        src0, src1, src2, src3, src4, 128);

        vec0 = __msa_vshf_b(mask0, src0, src0);
        vec1 = __msa_vshf_b(mask1, src0, src0);
        vec2 = __msa_vshf_b(mask2, src0, src0);
        vec3 = __msa_vshf_b(mask3, src0, src0);

        dst0 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0, dst);

        vec0 = __msa_vshf_b(mask4, src1, src0);
        vec1 = __msa_vshf_b(mask5, src1, src0);
        vec2 = __msa_vshf_b(mask6, src1, src0);
        vec3 = __msa_vshf_b(mask7, src1, src0);

        dst1 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst1, dst + 8);

        vec0 = __msa_vshf_b(mask0, src1, src1);
        vec1 = __msa_vshf_b(mask1, src1, src1);
        vec2 = __msa_vshf_b(mask2, src1, src1);
        vec3 = __msa_vshf_b(mask3, src1, src1);

        dst2 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst2, dst + 16);

        vec0 = __msa_vshf_b(mask4, src2, src1);
        vec1 = __msa_vshf_b(mask5, src2, src1);
        vec2 = __msa_vshf_b(mask6, src2, src1);
        vec3 = __msa_vshf_b(mask7, src2, src1);

        dst3 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst3, dst + 24);

        vec0 = __msa_vshf_b(mask0, src2, src2);
        vec1 = __msa_vshf_b(mask1, src2, src2);
        vec2 = __msa_vshf_b(mask2, src2, src2);
        vec3 = __msa_vshf_b(mask3, src2, src2);

        dst4 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst4, dst + 32);

        vec0 = __msa_vshf_b(mask4, src3, src2);
        vec1 = __msa_vshf_b(mask5, src3, src2);
        vec2 = __msa_vshf_b(mask6, src3, src2);
        vec3 = __msa_vshf_b(mask7, src3, src2);

        dst5 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst5, dst + 40);

        vec0 = __msa_vshf_b(mask0, src3, src3);
        vec1 = __msa_vshf_b(mask1, src3, src3);
        vec2 = __msa_vshf_b(mask2, src3, src3);
        vec3 = __msa_vshf_b(mask3, src3, src3);

        dst6 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst6, dst + 48);

        vec0 = __msa_vshf_b(mask0, src4, src4);
        vec1 = __msa_vshf_b(mask1, src4, src4);
        vec2 = __msa_vshf_b(mask2, src4, src4);
        vec3 = __msa_vshf_b(mask3, src4, src4);

        dst7 = HEVC_FILT_8TAP_DPADD_H(vec0, vec1, vec2, vec3,
                                      filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst7, dst + 56);

        dst += dst_stride;
    }
}

static void hevc_vt_8t_4w_msa(uint8_t * __restrict src, int32_t src_stride,
                              int16_t * __restrict dst, int32_t dst_stride,
                              const int8_t * __restrict filter, int32_t height)
{
    int32_t loop_cnt;
    uint64_t out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8;
    v16i8 src9, src10, src11, src12, src13, src14;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v16i8 src1110_r, src1211_r, src1312_r, src1413_r;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v16i8 src12111110, src14131312;
    v8i16 dst10, dst32, dst54, dst76;
    v8i16 filter_vec;
    v8i16 filt0, filt1, filt2, filt3;
    v8u16 const_vec;

    src -= (3 * src_stride);

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    LOAD_7VECS_SB(src, src_stride,
                  src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    ILVR_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                    src1, src3, src5, src2, src4, src6,
                    src10_r, src32_r, src54_r, src21_r, src43_r, src65_r);

    ILVR_D_3VECS_SB(src2110, src21_r, src10_r, src4332, src43_r, src32_r,
                    src6554, src65_r, src54_r);

    XORI_B_3VECS_SB(src2110, src4332, src6554, src2110, src4332, src6554, 128);

    for (loop_cnt = (height >> 3); loop_cnt--;) {
        LOAD_8VECS_SB(src, src_stride,
                      src7, src8, src9, src10, src11, src12, src13, src14);
        src += (8 * src_stride);

        ILVR_B_8VECS_SB(src6, src7, src8, src9, src10, src11, src12, src13,
                        src7, src8, src9, src10, src11, src12, src13, src14,
                        src76_r, src87_r, src98_r, src109_r,
                        src1110_r, src1211_r, src1312_r, src1413_r);

        ILVR_D_4VECS_SB(src8776, src87_r, src76_r, src10998, src109_r, src98_r,
                        src12111110, src1211_r, src1110_r,
                        src14131312, src1413_r, src1312_r);

        XORI_B_4VECS_SB(src8776, src10998, src12111110, src14131312,
                        src8776, src10998, src12111110, src14131312, 128);

        dst10 = HEVC_FILT_8TAP_DPADD_H(src2110, src4332, src6554, src8776,
                                       filt0, filt1, filt2, filt3, const_vec);

        dst32 = HEVC_FILT_8TAP_DPADD_H(src4332, src6554, src8776, src10998,
                                       filt0, filt1, filt2, filt3, const_vec);

        dst54 = HEVC_FILT_8TAP_DPADD_H(src6554, src8776, src10998, src12111110,
                                       filt0, filt1, filt2, filt3, const_vec);

        dst76 = HEVC_FILT_8TAP_DPADD_H(src8776, src10998,
                                       src12111110, src14131312,
                                       filt0, filt1, filt2, filt3, const_vec);

        out0 = __msa_copy_u_d((v2i64) dst10, 0);
        out1 = __msa_copy_u_d((v2i64) dst10, 1);
        out2 = __msa_copy_u_d((v2i64) dst32, 0);
        out3 = __msa_copy_u_d((v2i64) dst32, 1);

        STORE_DWORD(dst, out0);
        dst += dst_stride;
        STORE_DWORD(dst, out1);
        dst += dst_stride;
        STORE_DWORD(dst, out2);
        dst += dst_stride;
        STORE_DWORD(dst, out3);
        dst += dst_stride;

        out0 = __msa_copy_u_d((v2i64) dst54, 0);
        out1 = __msa_copy_u_d((v2i64) dst54, 1);
        out2 = __msa_copy_u_d((v2i64) dst76, 0);
        out3 = __msa_copy_u_d((v2i64) dst76, 1);

        STORE_DWORD(dst, out0);
        dst += dst_stride;
        STORE_DWORD(dst, out1);
        dst += dst_stride;
        STORE_DWORD(dst, out2);
        dst += dst_stride;
        STORE_DWORD(dst, out3);
        dst += dst_stride;

        src2110 = src10998;
        src4332 = src12111110;
        src6554 = src14131312;

        src6 = src14;
    }
}

static void hevc_vt_8t_8w_msa(uint8_t * __restrict src, int32_t src_stride,
                              int16_t * __restrict dst, int32_t dst_stride,
                              const int8_t * __restrict filter, int32_t height)
{
    int32_t loop_cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v8i16 filter_vec;
    v8i16 filt0, filt1, filt2, filt3;
    v8u16 const_vec;

    src -= (3 * src_stride);

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    LOAD_7VECS_SB(src, src_stride,
                  src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B_7VECS_SB(src0, src1, src2, src3, src4, src5, src6,
                    src0, src1, src2, src3, src4, src5, src6, 128);

    ILVR_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                    src1, src3, src5, src2, src4, src6,
                    src10_r, src32_r, src54_r, src21_r, src43_r, src65_r);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LOAD_4VECS_SB(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B_4VECS_SB(src7, src8, src9, src10, src7, src8, src9, src10, 128);

        ILVR_B_4VECS_SB(src6, src7, src8, src9, src7, src8, src9, src10,
                        src76_r, src87_r, src98_r, src109_r);

        dst0_r = HEVC_FILT_8TAP_DPADD_H(src10_r, src32_r, src54_r, src76_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst1_r = HEVC_FILT_8TAP_DPADD_H(src21_r, src43_r, src65_r, src87_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst2_r = HEVC_FILT_8TAP_DPADD_H(src32_r, src54_r, src76_r, src98_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst3_r = HEVC_FILT_8TAP_DPADD_H(src43_r, src65_r, src87_r, src109_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        STORE_SH(dst0_r, dst);
        dst += dst_stride;
        STORE_SH(dst1_r, dst);
        dst += dst_stride;
        STORE_SH(dst2_r, dst);
        dst += dst_stride;
        STORE_SH(dst3_r, dst);
        dst += dst_stride;

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;

        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;

        src6 = src10;
    }
}

static void hevc_vt_8t_12w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    int32_t loop_cnt;
    uint64_t out0, out1, out2, out3;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v16i8 src2110, src4332, src6554, src8776, src10998;
    v8i16 dst0_l, dst1_l;
    v8i16 filter_vec;
    v8i16 filt0, filt1, filt2, filt3;
    v8u16 const_vec;

    src -= (3 * src_stride);

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    LOAD_7VECS_SB(src, src_stride,
                  src0, src1, src2, src3, src4, src5, src6);
    src += (7 * src_stride);

    XORI_B_7VECS_SB(src0, src1, src2, src3, src4, src5, src6,
                    src0, src1, src2, src3, src4, src5, src6, 128);

    ILVR_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                    src1, src3, src5, src2, src4, src6,
                    src10_r, src32_r, src54_r, src21_r, src43_r, src65_r);

    ILVL_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                    src1, src3, src5, src2, src4, src6,
                    src10_l, src32_l, src54_l, src21_l, src43_l, src65_l);

    ILVR_D_3VECS_SB(src2110, src21_l, src10_l, src4332, src43_l, src32_l,
                    src6554, src65_l, src54_l);

    for (loop_cnt = (height >> 2); loop_cnt--;) {
        LOAD_4VECS_SB(src, src_stride, src7, src8, src9, src10);
        src += (4 * src_stride);

        XORI_B_4VECS_SB(src7, src8, src9, src10, src7, src8, src9, src10, 128);

        ILVR_B_4VECS_SB(src6, src7, src8, src9, src7, src8, src9, src10,
                        src76_r, src87_r, src98_r, src109_r);

        ILVL_B_4VECS_SB(src6, src7, src8, src9, src7, src8, src9, src10,
                        src76_l, src87_l, src98_l, src109_l);

        ILVR_D_2VECS_SB(src8776, src87_l, src76_l, src10998, src109_l, src98_l);

        dst0_r = HEVC_FILT_8TAP_DPADD_H(src10_r, src32_r, src54_r, src76_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst1_r = HEVC_FILT_8TAP_DPADD_H(src21_r, src43_r, src65_r, src87_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst2_r = HEVC_FILT_8TAP_DPADD_H(src32_r, src54_r, src76_r, src98_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst3_r = HEVC_FILT_8TAP_DPADD_H(src43_r, src65_r, src87_r, src109_r,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst0_l = HEVC_FILT_8TAP_DPADD_H(src2110, src4332, src6554, src8776,
                                        filt0, filt1, filt2, filt3, const_vec);

        dst1_l = HEVC_FILT_8TAP_DPADD_H(src4332, src6554, src8776, src10998,
                                        filt0, filt1, filt2, filt3, const_vec);

        out0 = __msa_copy_u_d((v2i64) dst0_l, 0);
        out1 = __msa_copy_u_d((v2i64) dst0_l, 1);
        out2 = __msa_copy_u_d((v2i64) dst1_l, 0);
        out3 = __msa_copy_u_d((v2i64) dst1_l, 1);

        STORE_SH(dst0_r, dst);
        STORE_DWORD(dst + 8, out0);
        dst += dst_stride;
        STORE_SH(dst1_r, dst);
        STORE_DWORD(dst + 8, out1);
        dst += dst_stride;

        STORE_SH(dst2_r, dst);
        STORE_DWORD(dst + 8, out2);
        dst += dst_stride;
        STORE_SH(dst3_r, dst);
        STORE_DWORD(dst + 8, out3);
        dst += dst_stride;

        src10_r = src54_r;
        src32_r = src76_r;
        src54_r = src98_r;

        src21_r = src65_r;
        src43_r = src87_r;
        src65_r = src109_r;

        src2110 = src6554;
        src4332 = src8776;
        src6554 = src10998;

        src6 = src10;
    }
}

static void hevc_vt_8t_16multx4mult_msa(uint8_t * __restrict src,
                                        int32_t src_stride,
                                        int16_t * __restrict dst,
                                        int32_t dst_stride,
                                        const int8_t * __restrict filter,
                                        int32_t height,
                                        int32_t width)
{
    uint8_t *src_tmp;
    int16_t *dst_tmp;
    int32_t loop_cnt, cnt;
    v16i8 src0, src1, src2, src3, src4, src5, src6, src7, src8, src9, src10;
    v16i8 src10_r, src32_r, src54_r, src76_r, src98_r;
    v16i8 src21_r, src43_r, src65_r, src87_r, src109_r;
    v8i16 dst0_r, dst1_r, dst2_r, dst3_r;
    v16i8 src10_l, src32_l, src54_l, src76_l, src98_l;
    v16i8 src21_l, src43_l, src65_l, src87_l, src109_l;
    v8i16 dst0_l, dst1_l, dst2_l, dst3_l;
    v8i16 filter_vec;
    v8i16 filt0, filt1, filt2, filt3;
    v8u16 const_vec;

    src -= (3 * src_stride);

    const_vec = (v8u16) __msa_ldi_h(128);
    const_vec <<= 6;

    filter_vec = LOAD_SH(filter);
    filt0 = __msa_splati_h(filter_vec, 0);
    filt1 = __msa_splati_h(filter_vec, 1);
    filt2 = __msa_splati_h(filter_vec, 2);
    filt3 = __msa_splati_h(filter_vec, 3);

    for (cnt = width >> 4; cnt--;) {
        src_tmp = src;
        dst_tmp = dst;

        LOAD_7VECS_SB(src_tmp, src_stride,
                      src0, src1, src2, src3, src4, src5, src6);
        src_tmp += (7 * src_stride);

        XORI_B_7VECS_SB(src0, src1, src2, src3, src4, src5, src6,
                        src0, src1, src2, src3, src4, src5, src6, 128);

        ILVR_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                        src1, src3, src5, src2, src4, src6,
                        src10_r, src32_r, src54_r, src21_r, src43_r, src65_r);

        ILVL_B_6VECS_SB(src0, src2, src4, src1, src3, src5,
                        src1, src3, src5, src2, src4, src6,
                        src10_l, src32_l, src54_l, src21_l, src43_l, src65_l);

        for (loop_cnt = (height >> 2); loop_cnt--;) {
            LOAD_4VECS_SB(src_tmp, src_stride, src7, src8, src9, src10);
            src_tmp += (4 * src_stride);

            XORI_B_4VECS_SB(src7, src8, src9, src10,
                            src7, src8, src9, src10, 128);

            ILVR_B_4VECS_SB(src6, src7, src8, src9, src7, src8, src9, src10,
                            src76_r, src87_r, src98_r, src109_r);

            ILVL_B_4VECS_SB(src6, src7, src8, src9, src7, src8, src9, src10,
                            src76_l, src87_l, src98_l, src109_l);

            dst0_r = HEVC_FILT_8TAP_DPADD_H(src10_r, src32_r, src54_r, src76_r,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst1_r = HEVC_FILT_8TAP_DPADD_H(src21_r, src43_r, src65_r, src87_r,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst2_r = HEVC_FILT_8TAP_DPADD_H(src32_r, src54_r, src76_r, src98_r,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst3_r = HEVC_FILT_8TAP_DPADD_H(src43_r, src65_r, src87_r, src109_r,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst0_l = HEVC_FILT_8TAP_DPADD_H(src10_l, src32_l, src54_l, src76_l,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst1_l = HEVC_FILT_8TAP_DPADD_H(src21_l, src43_l, src65_l, src87_l,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst2_l = HEVC_FILT_8TAP_DPADD_H(src32_l, src54_l, src76_l, src98_l,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            dst3_l = HEVC_FILT_8TAP_DPADD_H(src43_l, src65_l, src87_l, src109_l,
                                            filt0, filt1, filt2, filt3,
                                            const_vec);

            STORE_SH(dst0_r, dst_tmp);
            STORE_SH(dst0_l, dst_tmp + 8);
            dst_tmp += dst_stride;
            STORE_SH(dst1_r, dst_tmp);
            STORE_SH(dst1_l, dst_tmp + 8);
            dst_tmp += dst_stride;

            STORE_SH(dst2_r, dst_tmp);
            STORE_SH(dst2_l, dst_tmp + 8);
            dst_tmp += dst_stride;
            STORE_SH(dst3_r, dst_tmp);
            STORE_SH(dst3_l, dst_tmp + 8);
            dst_tmp += dst_stride;

            src10_r = src54_r;
            src32_r = src76_r;
            src54_r = src98_r;

            src21_r = src65_r;
            src43_r = src87_r;
            src65_r = src109_r;

            src10_l = src54_l;
            src32_l = src76_l;
            src54_l = src98_l;

            src21_l = src65_l;
            src43_l = src87_l;
            src65_l = src109_l;

            src6 = src10;
        }

        src += 16;
        dst += 16;
    }
}

static void hevc_vt_8t_16w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);
}

static void hevc_vt_8t_24w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 16);

    hevc_vt_8t_8w_msa(src + 16, src_stride, dst + 16, dst_stride,
                      filter, height);
}

static void hevc_vt_8t_32w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 32);
}

static void hevc_vt_8t_48w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 48);
}

static void hevc_vt_8t_64w_msa(uint8_t * __restrict src, int32_t src_stride,
                               int16_t * __restrict dst, int32_t dst_stride,
                               const int8_t * __restrict filter, int32_t height)
{
    hevc_vt_8t_16multx4mult_msa(src, src_stride, dst, dst_stride,
                                filter, height, 64);
}

#define MC(PEL, DIR, WIDTH, TAP, DIR1, FILT_DIR)                            \
void ff_hevc_put_hevc_##PEL##_##DIR####WIDTH##_8_msa(int16_t *dst,          \
                                                     uint8_t *src,          \
                                                     ptrdiff_t src_stride,  \
                                                     int height,            \
                                                     intptr_t mx,           \
                                                     intptr_t my,           \
                                                     int width)             \
{                                                                           \
    const int8_t *filter = ff_hevc_##PEL##_filters[FILT_DIR - 1];           \
                                                                            \
    hevc_##DIR1##_##TAP##t_##WIDTH##w_msa(src, src_stride, dst,             \
                                          MAX_PB_SIZE, filter, height);     \
}

MC(qpel, h, 4, 8, hz, mx);
MC(qpel, h, 8, 8, hz, mx);
MC(qpel, h, 12, 8, hz, mx);
MC(qpel, h, 16, 8, hz, mx);
MC(qpel, h, 24, 8, hz, mx);
MC(qpel, h, 32, 8, hz, mx);
MC(qpel, h, 48, 8, hz, mx);
MC(qpel, h, 64, 8, hz, mx);

MC(qpel, v, 4, 8, vt, my);
MC(qpel, v, 8, 8, vt, my);
MC(qpel, v, 12, 8, vt, my);
MC(qpel, v, 16, 8, vt, my);
MC(qpel, v, 24, 8, vt, my);
MC(qpel, v, 32, 8, vt, my);
MC(qpel, v, 48, 8, vt, my);
MC(qpel, v, 64, 8, vt, my);

#undef MC
