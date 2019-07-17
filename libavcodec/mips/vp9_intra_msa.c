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

#include "libavcodec/vp9dsp.h"
#include "libavutil/mips/generic_macros_msa.h"
#include "vp9dsp_mips.h"

#define IPRED_SUBS_UH2_UH(in0, in1, out0, out1)  \
{                                                \
    out0 = __msa_subs_u_h(out0, in0);            \
    out1 = __msa_subs_u_h(out1, in1);            \
}

void ff_vert_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *left,
                       const uint8_t *src)
{
    uint32_t row;
    v16u8 src0;

    src0 = LD_UB(src);

    for (row = 16; row--;) {
        ST_UB(src0, dst);
        dst += dst_stride;
    }
}

void ff_vert_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *left,
                       const uint8_t *src)
{
    uint32_t row;
    v16u8 src1, src2;

    src1 = LD_UB(src);
    src2 = LD_UB(src + 16);

    for (row = 32; row--;) {
        ST_UB2(src1, src2, dst, 16);
        dst += dst_stride;
    }
}

void ff_hor_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src,
                      const uint8_t *top)
{
    uint32_t row, inp;
    v16u8 src0, src1, src2, src3;

    src += 12;
    for (row = 4; row--;) {
        inp = LW(src);
        src -= 4;

        src0 = (v16u8) __msa_fill_b(inp >> 24);
        src1 = (v16u8) __msa_fill_b(inp >> 16);
        src2 = (v16u8) __msa_fill_b(inp >> 8);
        src3 = (v16u8) __msa_fill_b(inp);

        ST_UB4(src0, src1, src2, src3, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_hor_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src,
                      const uint8_t *top)
{
    uint32_t row, inp;
    v16u8 src0, src1, src2, src3;

    src += 28;
    for (row = 8; row--;) {
        inp = LW(src);
        src -= 4;

        src0 = (v16u8) __msa_fill_b(inp >> 24);
        src1 = (v16u8) __msa_fill_b(inp >> 16);
        src2 = (v16u8) __msa_fill_b(inp >> 8);
        src3 = (v16u8) __msa_fill_b(inp);

        ST_UB2(src0, src0, dst, 16);
        dst += dst_stride;
        ST_UB2(src1, src1, dst, 16);
        dst += dst_stride;
        ST_UB2(src2, src2, dst, 16);
        dst += dst_stride;
        ST_UB2(src3, src3, dst, 16);
        dst += dst_stride;
    }
}

void ff_dc_4x4_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src_left,
                   const uint8_t *src_top)
{
    uint32_t val0, val1;
    v16i8 store, src = { 0 };
    v8u16 sum_h;
    v4u32 sum_w;
    v2u64 sum_d;

    val0 = LW(src_top);
    val1 = LW(src_left);
    INSERT_W2_SB(val0, val1, src);
    sum_h = __msa_hadd_u_h((v16u8) src, (v16u8) src);
    sum_w = __msa_hadd_u_w(sum_h, sum_h);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 3);
    store = __msa_splati_b((v16i8) sum_w, 0);
    val0 = __msa_copy_u_w((v4i32) store, 0);

    SW4(val0, val0, val0, val0, dst, dst_stride);
}

#define INTRA_DC_TL_4x4(dir)                                    \
void ff_dc_##dir##_4x4_msa(uint8_t *dst, ptrdiff_t dst_stride,  \
                           const uint8_t *left,                 \
                           const uint8_t *top)                  \
{                                                               \
    uint32_t val0;                                              \
    v16i8 store, data = { 0 };                                  \
    v8u16 sum_h;                                                \
    v4u32 sum_w;                                                \
                                                                \
    val0 = LW(dir);                                             \
    data = (v16i8) __msa_insert_w((v4i32) data, 0, val0);       \
    sum_h = __msa_hadd_u_h((v16u8) data, (v16u8) data);         \
    sum_w = __msa_hadd_u_w(sum_h, sum_h);                       \
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_w, 2);            \
    store = __msa_splati_b((v16i8) sum_w, 0);                   \
    val0 = __msa_copy_u_w((v4i32) store, 0);                    \
                                                                \
    SW4(val0, val0, val0, val0, dst, dst_stride);               \
}
INTRA_DC_TL_4x4(top);
INTRA_DC_TL_4x4(left);

void ff_dc_8x8_msa(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src_left,
                   const uint8_t *src_top)
{
    uint64_t val0, val1;
    v16i8 store;
    v16u8 src = { 0 };
    v8u16 sum_h;
    v4u32 sum_w;
    v2u64 sum_d;

    val0 = LD(src_top);
    val1 = LD(src_left);
    INSERT_D2_UB(val0, val1, src);
    sum_h = __msa_hadd_u_h(src, src);
    sum_w = __msa_hadd_u_w(sum_h, sum_h);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_pckev_w((v4i32) sum_d, (v4i32) sum_d);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 4);
    store = __msa_splati_b((v16i8) sum_w, 0);
    val0 = __msa_copy_u_d((v2i64) store, 0);

    SD4(val0, val0, val0, val0, dst, dst_stride);
    dst += (4 * dst_stride);
    SD4(val0, val0, val0, val0, dst, dst_stride);
}

#define INTRA_DC_TL_8x8(dir)                                    \
void ff_dc_##dir##_8x8_msa(uint8_t *dst, ptrdiff_t dst_stride,  \
                           const uint8_t *left,                 \
                           const uint8_t *top)                  \
{                                                               \
    uint64_t val0;                                              \
    v16i8 store;                                                \
    v16u8 data = { 0 };                                         \
    v8u16 sum_h;                                                \
    v4u32 sum_w;                                                \
    v2u64 sum_d;                                                \
                                                                \
    val0 = LD(dir);                                             \
    data = (v16u8) __msa_insert_d((v2i64) data, 0, val0);       \
    sum_h = __msa_hadd_u_h(data, data);                         \
    sum_w = __msa_hadd_u_w(sum_h, sum_h);                       \
    sum_d = __msa_hadd_u_d(sum_w, sum_w);                       \
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 3);            \
    store = __msa_splati_b((v16i8) sum_w, 0);                   \
    val0 = __msa_copy_u_d((v2i64) store, 0);                    \
                                                                \
    SD4(val0, val0, val0, val0, dst, dst_stride);               \
    dst += (4 * dst_stride);                                    \
    SD4(val0, val0, val0, val0, dst, dst_stride);               \
}

INTRA_DC_TL_8x8(top);
INTRA_DC_TL_8x8(left);

void ff_dc_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top)
{
    v16u8 top, left, out;
    v8u16 sum_h, sum_top, sum_left;
    v4u32 sum_w;
    v2u64 sum_d;

    top = LD_UB(src_top);
    left = LD_UB(src_left);
    HADD_UB2_UH(top, left, sum_top, sum_left);
    sum_h = sum_top + sum_left;
    sum_w = __msa_hadd_u_w(sum_h, sum_h);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_pckev_w((v4i32) sum_d, (v4i32) sum_d);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 5);
    out = (v16u8) __msa_splati_b((v16i8) sum_w, 0);

    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);
    dst += (8 * dst_stride);
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);
}

#define INTRA_DC_TL_16x16(dir)                                        \
void ff_dc_##dir##_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride,      \
                             const uint8_t *left,                     \
                             const uint8_t *top)                      \
{                                                                     \
    v16u8 data, out;                                                  \
    v8u16 sum_h;                                                      \
    v4u32 sum_w;                                                      \
    v2u64 sum_d;                                                      \
                                                                      \
    data = LD_UB(dir);                                                \
    sum_h = __msa_hadd_u_h(data, data);                               \
    sum_w = __msa_hadd_u_w(sum_h, sum_h);                             \
    sum_d = __msa_hadd_u_d(sum_w, sum_w);                             \
    sum_w = (v4u32) __msa_pckev_w((v4i32) sum_d, (v4i32) sum_d);      \
    sum_d = __msa_hadd_u_d(sum_w, sum_w);                             \
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 4);                  \
    out = (v16u8) __msa_splati_b((v16i8) sum_w, 0);                   \
                                                                      \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);  \
    dst += (8 * dst_stride);                                          \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);  \
}
INTRA_DC_TL_16x16(top);
INTRA_DC_TL_16x16(left);

void ff_dc_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top)
{
    uint32_t row;
    v16u8 top0, top1, left0, left1, out;
    v8u16 sum_h, sum_top0, sum_top1, sum_left0, sum_left1;
    v4u32 sum_w;
    v2u64 sum_d;

    LD_UB2(src_top, 16, top0, top1);
    LD_UB2(src_left, 16, left0, left1);
    HADD_UB2_UH(top0, top1, sum_top0, sum_top1);
    HADD_UB2_UH(left0, left1, sum_left0, sum_left1);
    sum_h = sum_top0 + sum_top1;
    sum_h += sum_left0 + sum_left1;
    sum_w = __msa_hadd_u_w(sum_h, sum_h);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_pckev_w((v4i32) sum_d, (v4i32) sum_d);
    sum_d = __msa_hadd_u_d(sum_w, sum_w);
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 6);
    out = (v16u8) __msa_splati_b((v16i8) sum_w, 0);

    for (row = 16; row--;)
    {
        ST_UB2(out, out, dst, 16);
        dst += dst_stride;
        ST_UB2(out, out, dst, 16);
        dst += dst_stride;
    }
}

#define INTRA_DC_TL_32x32(dir)                                    \
void ff_dc_##dir##_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride,  \
                             const uint8_t *left,                 \
                             const uint8_t *top)                  \
{                                                                 \
    uint32_t row;                                                 \
    v16u8 data0, data1, out;                                      \
    v8u16 sum_h, sum_data0, sum_data1;                            \
    v4u32 sum_w;                                                  \
    v2u64 sum_d;                                                  \
                                                                  \
    LD_UB2(dir, 16, data0, data1);                                \
    HADD_UB2_UH(data0, data1, sum_data0, sum_data1);              \
    sum_h = sum_data0 + sum_data1;                                \
    sum_w = __msa_hadd_u_w(sum_h, sum_h);                         \
    sum_d = __msa_hadd_u_d(sum_w, sum_w);                         \
    sum_w = (v4u32) __msa_pckev_w((v4i32) sum_d, (v4i32) sum_d);  \
    sum_d = __msa_hadd_u_d(sum_w, sum_w);                         \
    sum_w = (v4u32) __msa_srari_w((v4i32) sum_d, 5);              \
    out = (v16u8) __msa_splati_b((v16i8) sum_w, 0);               \
                                                                  \
    for (row = 16; row--;)                                        \
    {                                                             \
        ST_UB2(out, out, dst, 16);                                \
        dst += dst_stride;                                        \
        ST_UB2(out, out, dst, 16);                                \
        dst += dst_stride;                                        \
    }                                                             \
}
INTRA_DC_TL_32x32(top);
INTRA_DC_TL_32x32(left);

#define INTRA_PREDICT_VALDC_16X16_MSA(val)                             \
void ff_dc_##val##_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride,       \
                             const uint8_t *left, const uint8_t *top)  \
{                                                                      \
    v16u8 out = (v16u8) __msa_ldi_b(val);                              \
                                                                       \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);   \
    dst += (8 * dst_stride);                                           \
    ST_UB8(out, out, out, out, out, out, out, out, dst, dst_stride);   \
}

INTRA_PREDICT_VALDC_16X16_MSA(127);
INTRA_PREDICT_VALDC_16X16_MSA(128);
INTRA_PREDICT_VALDC_16X16_MSA(129);

#define INTRA_PREDICT_VALDC_32X32_MSA(val)                             \
void ff_dc_##val##_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride,       \
                             const uint8_t *left, const uint8_t *top)  \
{                                                                      \
    uint32_t row;                                                      \
    v16u8 out = (v16u8) __msa_ldi_b(val);                              \
                                                                       \
    for (row = 16; row--;)                                             \
    {                                                                  \
        ST_UB2(out, out, dst, 16);                                     \
        dst += dst_stride;                                             \
        ST_UB2(out, out, dst, 16);                                     \
        dst += dst_stride;                                             \
    }                                                                  \
}

INTRA_PREDICT_VALDC_32X32_MSA(127);
INTRA_PREDICT_VALDC_32X32_MSA(128);
INTRA_PREDICT_VALDC_32X32_MSA(129);

void ff_tm_4x4_msa(uint8_t *dst, ptrdiff_t dst_stride,
                   const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint32_t left;
    uint8_t top_left = src_top_ptr[-1];
    v16i8 src_top, src_left0, src_left1, src_left2, src_left3, tmp0, tmp1;
    v16u8 src0, src1, src2, src3;
    v8u16 src_top_left, vec0, vec1, vec2, vec3;

    src_top_left = (v8u16) __msa_fill_h(top_left);
    src_top = LD_SB(src_top_ptr);
    left = LW(src_left);
    src_left0 = __msa_fill_b(left >> 24);
    src_left1 = __msa_fill_b(left >> 16);
    src_left2 = __msa_fill_b(left >> 8);
    src_left3 = __msa_fill_b(left);

    ILVR_B4_UB(src_left0, src_top, src_left1, src_top, src_left2, src_top,
               src_left3, src_top, src0, src1, src2, src3);
    HADD_UB4_UH(src0, src1, src2, src3, vec0, vec1, vec2, vec3);
    IPRED_SUBS_UH2_UH(src_top_left, src_top_left, vec0, vec1);
    IPRED_SUBS_UH2_UH(src_top_left, src_top_left, vec2, vec3);
    SAT_UH4_UH(vec0, vec1, vec2, vec3, 7);
    PCKEV_B2_SB(vec1, vec0, vec3, vec2, tmp0, tmp1);
    ST_W2(tmp0, 0, 2, dst, dst_stride);
    ST_W2(tmp1, 0, 2, dst + 2 * dst_stride, dst_stride);
}

void ff_tm_8x8_msa(uint8_t *dst, ptrdiff_t dst_stride,
                   const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    uint32_t loop_cnt, left;
    v16i8 src_top, src_left0, src_left1, src_left2, src_left3, tmp0, tmp1;
    v8u16 src_top_left, vec0, vec1, vec2, vec3;
    v16u8 src0, src1, src2, src3;

    src_top = LD_SB(src_top_ptr);
    src_top_left = (v8u16) __msa_fill_h(top_left);

    src_left += 4;
    for (loop_cnt = 2; loop_cnt--;) {
        left = LW(src_left);
        src_left0 = __msa_fill_b(left >> 24);
        src_left1 = __msa_fill_b(left >> 16);
        src_left2 = __msa_fill_b(left >> 8);
        src_left3 = __msa_fill_b(left);
        src_left -= 4;

        ILVR_B4_UB(src_left0, src_top, src_left1, src_top, src_left2, src_top,
                   src_left3, src_top, src0, src1, src2, src3);
        HADD_UB4_UH(src0, src1, src2, src3, vec0, vec1, vec2, vec3);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, vec0, vec1);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, vec2, vec3);
        SAT_UH4_UH(vec0, vec1, vec2, vec3, 7);
        PCKEV_B2_SB(vec1, vec0, vec3, vec2, tmp0, tmp1);
        ST_D4(tmp0, tmp1, 0, 1, 0, 1, dst, dst_stride);
        dst += (4 * dst_stride);
    }
}

void ff_tm_16x16_msa(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    uint32_t loop_cnt, left;
    v16i8 src_top, src_left0, src_left1, src_left2, src_left3;
    v8u16 src_top_left, res_r, res_l;

    src_top = LD_SB(src_top_ptr);
    src_top_left = (v8u16) __msa_fill_h(top_left);

    src_left += 12;
    for (loop_cnt = 4; loop_cnt--;) {
        left = LW(src_left);
        src_left0 = __msa_fill_b(left >> 24);
        src_left1 = __msa_fill_b(left >> 16);
        src_left2 = __msa_fill_b(left >> 8);
        src_left3 = __msa_fill_b(left);
        src_left -= 4;

        ILVRL_B2_UH(src_left0, src_top, res_r, res_l);
        HADD_UB2_UH(res_r, res_l, res_r, res_l);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r, res_l);

        SAT_UH2_UH(res_r, res_l, 7);
        PCKEV_ST_SB(res_r, res_l, dst);
        dst += dst_stride;

        ILVRL_B2_UH(src_left1, src_top, res_r, res_l);
        HADD_UB2_UH(res_r, res_l, res_r, res_l);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r, res_l);
        SAT_UH2_UH(res_r, res_l, 7);
        PCKEV_ST_SB(res_r, res_l, dst);
        dst += dst_stride;

        ILVRL_B2_UH(src_left2, src_top, res_r, res_l);
        HADD_UB2_UH(res_r, res_l, res_r, res_l);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r, res_l);
        SAT_UH2_UH(res_r, res_l, 7);
        PCKEV_ST_SB(res_r, res_l, dst);
        dst += dst_stride;

        ILVRL_B2_UH(src_left3, src_top, res_r, res_l);
        HADD_UB2_UH(res_r, res_l, res_r, res_l);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r, res_l);
        SAT_UH2_UH(res_r, res_l, 7);
        PCKEV_ST_SB(res_r, res_l, dst);
        dst += dst_stride;
    }
}

void ff_tm_32x32_msa(uint8_t *dst, ptrdiff_t dst_stride,
                     const uint8_t *src_left, const uint8_t *src_top_ptr)
{
    uint8_t top_left = src_top_ptr[-1];
    uint32_t loop_cnt, left;
    v16i8 src_top0, src_top1, src_left0, src_left1, src_left2, src_left3;
    v8u16 src_top_left, res_r0, res_r1, res_l0, res_l1;

    src_top0 = LD_SB(src_top_ptr);
    src_top1 = LD_SB(src_top_ptr + 16);
    src_top_left = (v8u16) __msa_fill_h(top_left);

    src_left += 28;
    for (loop_cnt = 8; loop_cnt--;) {
        left = LW(src_left);
        src_left0 = __msa_fill_b(left >> 24);
        src_left1 = __msa_fill_b(left >> 16);
        src_left2 = __msa_fill_b(left >> 8);
        src_left3 = __msa_fill_b(left);
        src_left -= 4;

        ILVR_B2_UH(src_left0, src_top0, src_left0, src_top1, res_r0, res_r1);
        ILVL_B2_UH(src_left0, src_top0, src_left0, src_top1, res_l0, res_l1);
        HADD_UB4_UH(res_r0, res_l0, res_r1, res_l1, res_r0, res_l0, res_r1,
                    res_l1);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r0, res_l0);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r1, res_l1);
        SAT_UH4_UH(res_r0, res_l0, res_r1, res_l1, 7);
        PCKEV_ST_SB(res_r0, res_l0, dst);
        PCKEV_ST_SB(res_r1, res_l1, dst + 16);
        dst += dst_stride;

        ILVR_B2_UH(src_left1, src_top0, src_left1, src_top1, res_r0, res_r1);
        ILVL_B2_UH(src_left1, src_top0, src_left1, src_top1, res_l0, res_l1);
        HADD_UB4_UH(res_r0, res_l0, res_r1, res_l1, res_r0, res_l0, res_r1,
                    res_l1);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r0, res_l0);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r1, res_l1);
        SAT_UH4_UH(res_r0, res_l0, res_r1, res_l1, 7);
        PCKEV_ST_SB(res_r0, res_l0, dst);
        PCKEV_ST_SB(res_r1, res_l1, dst + 16);
        dst += dst_stride;

        ILVR_B2_UH(src_left2, src_top0, src_left2, src_top1, res_r0, res_r1);
        ILVL_B2_UH(src_left2, src_top0, src_left2, src_top1, res_l0, res_l1);
        HADD_UB4_UH(res_r0, res_l0, res_r1, res_l1, res_r0, res_l0, res_r1,
                    res_l1);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r0, res_l0);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r1, res_l1);
        SAT_UH4_UH(res_r0, res_l0, res_r1, res_l1, 7);
        PCKEV_ST_SB(res_r0, res_l0, dst);
        PCKEV_ST_SB(res_r1, res_l1, dst + 16);
        dst += dst_stride;

        ILVR_B2_UH(src_left3, src_top0, src_left3, src_top1, res_r0, res_r1);
        ILVL_B2_UH(src_left3, src_top0, src_left3, src_top1, res_l0, res_l1);
        HADD_UB4_UH(res_r0, res_l0, res_r1, res_l1, res_r0, res_l0, res_r1,
                    res_l1);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r0, res_l0);
        IPRED_SUBS_UH2_UH(src_top_left, src_top_left, res_r1, res_l1);
        SAT_UH4_UH(res_r0, res_l0, res_r1, res_l1, 7);
        PCKEV_ST_SB(res_r0, res_l0, dst);
        PCKEV_ST_SB(res_r1, res_l1, dst + 16);
        dst += dst_stride;
    }
}
