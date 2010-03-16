/*
 * Copyright (c) 2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "libavcodec/dsputil.h"
#include "libavcodec/h264data.h"
#include "libavcodec/h264dsp.h"

#include "dsputil_ppc.h"
#include "dsputil_altivec.h"
#include "util_altivec.h"
#include "types_altivec.h"

#define PUT_OP_U8_ALTIVEC(d, s, dst) d = s
#define AVG_OP_U8_ALTIVEC(d, s, dst) d = vec_avg(dst, s)

#define OP_U8_ALTIVEC                          PUT_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         put_h264_chroma_mc8_altivec
#define PREFIX_no_rnd_vc1_chroma_mc8_altivec   put_no_rnd_vc1_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_put_h264_chroma_mc8_num
#define PREFIX_h264_qpel16_h_lowpass_altivec   put_h264_qpel16_h_lowpass_altivec
#define PREFIX_h264_qpel16_h_lowpass_num       altivec_put_h264_qpel16_h_lowpass_num
#define PREFIX_h264_qpel16_v_lowpass_altivec   put_h264_qpel16_v_lowpass_altivec
#define PREFIX_h264_qpel16_v_lowpass_num       altivec_put_h264_qpel16_v_lowpass_num
#define PREFIX_h264_qpel16_hv_lowpass_altivec  put_h264_qpel16_hv_lowpass_altivec
#define PREFIX_h264_qpel16_hv_lowpass_num      altivec_put_h264_qpel16_hv_lowpass_num
#include "h264_template_altivec.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_no_rnd_vc1_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num
#undef PREFIX_h264_qpel16_h_lowpass_altivec
#undef PREFIX_h264_qpel16_h_lowpass_num
#undef PREFIX_h264_qpel16_v_lowpass_altivec
#undef PREFIX_h264_qpel16_v_lowpass_num
#undef PREFIX_h264_qpel16_hv_lowpass_altivec
#undef PREFIX_h264_qpel16_hv_lowpass_num

#define OP_U8_ALTIVEC                          AVG_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         avg_h264_chroma_mc8_altivec
#define PREFIX_no_rnd_vc1_chroma_mc8_altivec   avg_no_rnd_vc1_chroma_mc8_altivec
#define PREFIX_h264_chroma_mc8_num             altivec_avg_h264_chroma_mc8_num
#define PREFIX_h264_qpel16_h_lowpass_altivec   avg_h264_qpel16_h_lowpass_altivec
#define PREFIX_h264_qpel16_h_lowpass_num       altivec_avg_h264_qpel16_h_lowpass_num
#define PREFIX_h264_qpel16_v_lowpass_altivec   avg_h264_qpel16_v_lowpass_altivec
#define PREFIX_h264_qpel16_v_lowpass_num       altivec_avg_h264_qpel16_v_lowpass_num
#define PREFIX_h264_qpel16_hv_lowpass_altivec  avg_h264_qpel16_hv_lowpass_altivec
#define PREFIX_h264_qpel16_hv_lowpass_num      altivec_avg_h264_qpel16_hv_lowpass_num
#include "h264_template_altivec.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_h264_chroma_mc8_altivec
#undef PREFIX_no_rnd_vc1_chroma_mc8_altivec
#undef PREFIX_h264_chroma_mc8_num
#undef PREFIX_h264_qpel16_h_lowpass_altivec
#undef PREFIX_h264_qpel16_h_lowpass_num
#undef PREFIX_h264_qpel16_v_lowpass_altivec
#undef PREFIX_h264_qpel16_v_lowpass_num
#undef PREFIX_h264_qpel16_hv_lowpass_altivec
#undef PREFIX_h264_qpel16_hv_lowpass_num

#define H264_MC(OPNAME, SIZE, CODETYPE) \
static void OPNAME ## h264_qpel ## SIZE ## _mc00_ ## CODETYPE (uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## pixels ## SIZE ## _ ## CODETYPE(dst, src, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc10_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){ \
    DECLARE_ALIGNED(16, uint8_t, half)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, half)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+1, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, half)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, half)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+stride, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, int16_t, tmp)[SIZE*(SIZE+8)];\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(dst, tmp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfHV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, int16_t, tmp)[SIZE*(SIZE+8)];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfH)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfHV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, int16_t, tmp)[SIZE*(SIZE+8)];\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfHV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, int16_t, tmp)[SIZE*(SIZE+8)];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED(16, uint8_t, halfV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, uint8_t, halfHV)[SIZE*SIZE];\
    DECLARE_ALIGNED(16, int16_t, tmp)[SIZE*(SIZE+8)];\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\

static inline void put_pixels16_l2_altivec( uint8_t * dst, const uint8_t * src1,
                                    const uint8_t * src2, int dst_stride,
                                    int src_stride1, int h)
{
    int i;
    vec_u8 a, b, d, tmp1, tmp2, mask, mask_, edges, align;

    mask_ = vec_lvsl(0, src2);

    for (i = 0; i < h; i++) {

        tmp1 = vec_ld(i * src_stride1, src1);
        mask = vec_lvsl(i * src_stride1, src1);
        tmp2 = vec_ld(i * src_stride1 + 15, src1);

        a = vec_perm(tmp1, tmp2, mask);

        tmp1 = vec_ld(i * 16, src2);
        tmp2 = vec_ld(i * 16 + 15, src2);

        b = vec_perm(tmp1, tmp2, mask_);

        tmp1 = vec_ld(0, dst);
        mask = vec_lvsl(0, dst);
        tmp2 = vec_ld(15, dst);

        d = vec_avg(a, b);

        edges = vec_perm(tmp2, tmp1, mask);

        align = vec_lvsr(0, dst);

        tmp2 = vec_perm(d, edges, align);
        tmp1 = vec_perm(edges, d, align);

        vec_st(tmp2, 15, dst);
        vec_st(tmp1, 0 , dst);

        dst += dst_stride;
    }
}

static inline void avg_pixels16_l2_altivec( uint8_t * dst, const uint8_t * src1,
                                    const uint8_t * src2, int dst_stride,
                                    int src_stride1, int h)
{
    int i;
    vec_u8 a, b, d, tmp1, tmp2, mask, mask_, edges, align;

    mask_ = vec_lvsl(0, src2);

    for (i = 0; i < h; i++) {

        tmp1 = vec_ld(i * src_stride1, src1);
        mask = vec_lvsl(i * src_stride1, src1);
        tmp2 = vec_ld(i * src_stride1 + 15, src1);

        a = vec_perm(tmp1, tmp2, mask);

        tmp1 = vec_ld(i * 16, src2);
        tmp2 = vec_ld(i * 16 + 15, src2);

        b = vec_perm(tmp1, tmp2, mask_);

        tmp1 = vec_ld(0, dst);
        mask = vec_lvsl(0, dst);
        tmp2 = vec_ld(15, dst);

        d = vec_avg(vec_perm(tmp1, tmp2, mask), vec_avg(a, b));

        edges = vec_perm(tmp2, tmp1, mask);

        align = vec_lvsr(0, dst);

        tmp2 = vec_perm(d, edges, align);
        tmp1 = vec_perm(edges, d, align);

        vec_st(tmp2, 15, dst);
        vec_st(tmp1, 0 , dst);

        dst += dst_stride;
    }
}

/* Implemented but could be faster
#define put_pixels16_l2_altivec(d,s1,s2,ds,s1s,h) put_pixels16_l2(d,s1,s2,ds,s1s,16,h)
#define avg_pixels16_l2_altivec(d,s1,s2,ds,s1s,h) avg_pixels16_l2(d,s1,s2,ds,s1s,16,h)
 */

H264_MC(put_, 16, altivec)
H264_MC(avg_, 16, altivec)


/****************************************************************************
 * IDCT transform:
 ****************************************************************************/

#define VEC_1D_DCT(vb0,vb1,vb2,vb3,va0,va1,va2,va3)               \
    /* 1st stage */                                               \
    vz0 = vec_add(vb0,vb2);       /* temp[0] = Y[0] + Y[2] */     \
    vz1 = vec_sub(vb0,vb2);       /* temp[1] = Y[0] - Y[2] */     \
    vz2 = vec_sra(vb1,vec_splat_u16(1));                          \
    vz2 = vec_sub(vz2,vb3);       /* temp[2] = Y[1].1/2 - Y[3] */ \
    vz3 = vec_sra(vb3,vec_splat_u16(1));                          \
    vz3 = vec_add(vb1,vz3);       /* temp[3] = Y[1] + Y[3].1/2 */ \
    /* 2nd stage: output */                                       \
    va0 = vec_add(vz0,vz3);       /* x[0] = temp[0] + temp[3] */  \
    va1 = vec_add(vz1,vz2);       /* x[1] = temp[1] + temp[2] */  \
    va2 = vec_sub(vz1,vz2);       /* x[2] = temp[1] - temp[2] */  \
    va3 = vec_sub(vz0,vz3)        /* x[3] = temp[0] - temp[3] */

#define VEC_TRANSPOSE_4(a0,a1,a2,a3,b0,b1,b2,b3) \
    b0 = vec_mergeh( a0, a0 ); \
    b1 = vec_mergeh( a1, a0 ); \
    b2 = vec_mergeh( a2, a0 ); \
    b3 = vec_mergeh( a3, a0 ); \
    a0 = vec_mergeh( b0, b2 ); \
    a1 = vec_mergel( b0, b2 ); \
    a2 = vec_mergeh( b1, b3 ); \
    a3 = vec_mergel( b1, b3 ); \
    b0 = vec_mergeh( a0, a2 ); \
    b1 = vec_mergel( a0, a2 ); \
    b2 = vec_mergeh( a1, a3 ); \
    b3 = vec_mergel( a1, a3 )

#define VEC_LOAD_U8_ADD_S16_STORE_U8(va)                      \
    vdst_orig = vec_ld(0, dst);                               \
    vdst = vec_perm(vdst_orig, zero_u8v, vdst_mask);          \
    vdst_ss = (vec_s16) vec_mergeh(zero_u8v, vdst);         \
    va = vec_add(va, vdst_ss);                                \
    va_u8 = vec_packsu(va, zero_s16v);                        \
    va_u32 = vec_splat((vec_u32)va_u8, 0);                  \
    vec_ste(va_u32, element, (uint32_t*)dst);

static void ff_h264_idct_add_altivec(uint8_t *dst, DCTELEM *block, int stride)
{
    vec_s16 va0, va1, va2, va3;
    vec_s16 vz0, vz1, vz2, vz3;
    vec_s16 vtmp0, vtmp1, vtmp2, vtmp3;
    vec_u8 va_u8;
    vec_u32 va_u32;
    vec_s16 vdst_ss;
    const vec_u16 v6us = vec_splat_u16(6);
    vec_u8 vdst, vdst_orig;
    vec_u8 vdst_mask = vec_lvsl(0, dst);
    int element = ((unsigned long)dst & 0xf) >> 2;
    LOAD_ZERO;

    block[0] += 32;  /* add 32 as a DC-level for rounding */

    vtmp0 = vec_ld(0,block);
    vtmp1 = vec_sld(vtmp0, vtmp0, 8);
    vtmp2 = vec_ld(16,block);
    vtmp3 = vec_sld(vtmp2, vtmp2, 8);

    VEC_1D_DCT(vtmp0,vtmp1,vtmp2,vtmp3,va0,va1,va2,va3);
    VEC_TRANSPOSE_4(va0,va1,va2,va3,vtmp0,vtmp1,vtmp2,vtmp3);
    VEC_1D_DCT(vtmp0,vtmp1,vtmp2,vtmp3,va0,va1,va2,va3);

    va0 = vec_sra(va0,v6us);
    va1 = vec_sra(va1,v6us);
    va2 = vec_sra(va2,v6us);
    va3 = vec_sra(va3,v6us);

    VEC_LOAD_U8_ADD_S16_STORE_U8(va0);
    dst += stride;
    VEC_LOAD_U8_ADD_S16_STORE_U8(va1);
    dst += stride;
    VEC_LOAD_U8_ADD_S16_STORE_U8(va2);
    dst += stride;
    VEC_LOAD_U8_ADD_S16_STORE_U8(va3);
}

#define IDCT8_1D_ALTIVEC(s0, s1, s2, s3, s4, s5, s6, s7,  d0, d1, d2, d3, d4, d5, d6, d7) {\
    /*        a0  = SRC(0) + SRC(4); */ \
    vec_s16 a0v = vec_add(s0, s4);    \
    /*        a2  = SRC(0) - SRC(4); */ \
    vec_s16 a2v = vec_sub(s0, s4);    \
    /*        a4  =           (SRC(2)>>1) - SRC(6); */ \
    vec_s16 a4v = vec_sub(vec_sra(s2, onev), s6);    \
    /*        a6  =           (SRC(6)>>1) + SRC(2); */ \
    vec_s16 a6v = vec_add(vec_sra(s6, onev), s2);    \
    /*        b0  =         a0 + a6; */ \
    vec_s16 b0v = vec_add(a0v, a6v);  \
    /*        b2  =         a2 + a4; */ \
    vec_s16 b2v = vec_add(a2v, a4v);  \
    /*        b4  =         a2 - a4; */ \
    vec_s16 b4v = vec_sub(a2v, a4v);  \
    /*        b6  =         a0 - a6; */ \
    vec_s16 b6v = vec_sub(a0v, a6v);  \
    /* a1 =  SRC(5) - SRC(3) - SRC(7) - (SRC(7)>>1); */ \
    /*        a1 =             (SRC(5)-SRC(3)) -  (SRC(7)  +  (SRC(7)>>1)); */ \
    vec_s16 a1v = vec_sub( vec_sub(s5, s3), vec_add(s7, vec_sra(s7, onev)) ); \
    /* a3 =  SRC(7) + SRC(1) - SRC(3) - (SRC(3)>>1); */ \
    /*        a3 =             (SRC(7)+SRC(1)) -  (SRC(3)  +  (SRC(3)>>1)); */ \
    vec_s16 a3v = vec_sub( vec_add(s7, s1), vec_add(s3, vec_sra(s3, onev)) );\
    /* a5 =  SRC(7) - SRC(1) + SRC(5) + (SRC(5)>>1); */ \
    /*        a5 =             (SRC(7)-SRC(1)) +   SRC(5) +   (SRC(5)>>1); */ \
    vec_s16 a5v = vec_add( vec_sub(s7, s1), vec_add(s5, vec_sra(s5, onev)) );\
    /*        a7 =                SRC(5)+SRC(3) +  SRC(1) +   (SRC(1)>>1); */ \
    vec_s16 a7v = vec_add( vec_add(s5, s3), vec_add(s1, vec_sra(s1, onev)) );\
    /*        b1 =                  (a7>>2)  +  a1; */ \
    vec_s16 b1v = vec_add( vec_sra(a7v, twov), a1v); \
    /*        b3 =          a3 +        (a5>>2); */ \
    vec_s16 b3v = vec_add(a3v, vec_sra(a5v, twov)); \
    /*        b5 =                  (a3>>2)  -   a5; */ \
    vec_s16 b5v = vec_sub( vec_sra(a3v, twov), a5v); \
    /*        b7 =           a7 -        (a1>>2); */ \
    vec_s16 b7v = vec_sub( a7v, vec_sra(a1v, twov)); \
    /* DST(0,    b0 + b7); */ \
    d0 = vec_add(b0v, b7v); \
    /* DST(1,    b2 + b5); */ \
    d1 = vec_add(b2v, b5v); \
    /* DST(2,    b4 + b3); */ \
    d2 = vec_add(b4v, b3v); \
    /* DST(3,    b6 + b1); */ \
    d3 = vec_add(b6v, b1v); \
    /* DST(4,    b6 - b1); */ \
    d4 = vec_sub(b6v, b1v); \
    /* DST(5,    b4 - b3); */ \
    d5 = vec_sub(b4v, b3v); \
    /* DST(6,    b2 - b5); */ \
    d6 = vec_sub(b2v, b5v); \
    /* DST(7,    b0 - b7); */ \
    d7 = vec_sub(b0v, b7v); \
}

#define ALTIVEC_STORE_SUM_CLIP(dest, idctv, perm_ldv, perm_stv, sel) { \
    /* unaligned load */                                       \
    vec_u8 hv = vec_ld( 0, dest );                           \
    vec_u8 lv = vec_ld( 7, dest );                           \
    vec_u8 dstv   = vec_perm( hv, lv, (vec_u8)perm_ldv );  \
    vec_s16 idct_sh6 = vec_sra(idctv, sixv);                 \
    vec_u16 dst16 = (vec_u16)vec_mergeh(zero_u8v, dstv);   \
    vec_s16 idstsum = vec_adds(idct_sh6, (vec_s16)dst16);  \
    vec_u8 idstsum8 = vec_packsu(zero_s16v, idstsum);        \
    vec_u8 edgehv;                                           \
    /* unaligned store */                                      \
    vec_u8 bodyv  = vec_perm( idstsum8, idstsum8, perm_stv );\
    vec_u8 edgelv = vec_perm( sel, zero_u8v, perm_stv );     \
    lv    = vec_sel( lv, bodyv, edgelv );                      \
    vec_st( lv, 7, dest );                                     \
    hv    = vec_ld( 0, dest );                                 \
    edgehv = vec_perm( zero_u8v, sel, perm_stv );              \
    hv    = vec_sel( hv, bodyv, edgehv );                      \
    vec_st( hv, 0, dest );                                     \
 }

static void ff_h264_idct8_add_altivec( uint8_t *dst, DCTELEM *dct, int stride ) {
    vec_s16 s0, s1, s2, s3, s4, s5, s6, s7;
    vec_s16 d0, d1, d2, d3, d4, d5, d6, d7;
    vec_s16 idct0, idct1, idct2, idct3, idct4, idct5, idct6, idct7;

    vec_u8 perm_ldv = vec_lvsl(0, dst);
    vec_u8 perm_stv = vec_lvsr(8, dst);

    const vec_u16 onev = vec_splat_u16(1);
    const vec_u16 twov = vec_splat_u16(2);
    const vec_u16 sixv = vec_splat_u16(6);

    const vec_u8 sel = (vec_u8) {0,0,0,0,0,0,0,0,-1,-1,-1,-1,-1,-1,-1,-1};
    LOAD_ZERO;

    dct[0] += 32; // rounding for the >>6 at the end

    s0 = vec_ld(0x00, (int16_t*)dct);
    s1 = vec_ld(0x10, (int16_t*)dct);
    s2 = vec_ld(0x20, (int16_t*)dct);
    s3 = vec_ld(0x30, (int16_t*)dct);
    s4 = vec_ld(0x40, (int16_t*)dct);
    s5 = vec_ld(0x50, (int16_t*)dct);
    s6 = vec_ld(0x60, (int16_t*)dct);
    s7 = vec_ld(0x70, (int16_t*)dct);

    IDCT8_1D_ALTIVEC(s0, s1, s2, s3, s4, s5, s6, s7,
                     d0, d1, d2, d3, d4, d5, d6, d7);

    TRANSPOSE8( d0,  d1,  d2,  d3,  d4,  d5,  d6, d7 );

    IDCT8_1D_ALTIVEC(d0,  d1,  d2,  d3,  d4,  d5,  d6, d7,
                     idct0, idct1, idct2, idct3, idct4, idct5, idct6, idct7);

    ALTIVEC_STORE_SUM_CLIP(&dst[0*stride], idct0, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[1*stride], idct1, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[2*stride], idct2, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[3*stride], idct3, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[4*stride], idct4, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[5*stride], idct5, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[6*stride], idct6, perm_ldv, perm_stv, sel);
    ALTIVEC_STORE_SUM_CLIP(&dst[7*stride], idct7, perm_ldv, perm_stv, sel);
}

static av_always_inline void h264_idct_dc_add_internal(uint8_t *dst, DCTELEM *block, int stride, int size)
{
    vec_s16 dc16;
    vec_u8 dcplus, dcminus, v0, v1, v2, v3, aligner;
    LOAD_ZERO;
    DECLARE_ALIGNED(16, int, dc);
    int i;

    dc = (block[0] + 32) >> 6;
    dc16 = vec_splat((vec_s16) vec_lde(0, &dc), 1);

    if (size == 4)
        dc16 = vec_sld(dc16, zero_s16v, 8);
    dcplus = vec_packsu(dc16, zero_s16v);
    dcminus = vec_packsu(vec_sub(zero_s16v, dc16), zero_s16v);

    aligner = vec_lvsr(0, dst);
    dcplus = vec_perm(dcplus, dcplus, aligner);
    dcminus = vec_perm(dcminus, dcminus, aligner);

    for (i = 0; i < size; i += 4) {
        v0 = vec_ld(0, dst+0*stride);
        v1 = vec_ld(0, dst+1*stride);
        v2 = vec_ld(0, dst+2*stride);
        v3 = vec_ld(0, dst+3*stride);

        v0 = vec_adds(v0, dcplus);
        v1 = vec_adds(v1, dcplus);
        v2 = vec_adds(v2, dcplus);
        v3 = vec_adds(v3, dcplus);

        v0 = vec_subs(v0, dcminus);
        v1 = vec_subs(v1, dcminus);
        v2 = vec_subs(v2, dcminus);
        v3 = vec_subs(v3, dcminus);

        vec_st(v0, 0, dst+0*stride);
        vec_st(v1, 0, dst+1*stride);
        vec_st(v2, 0, dst+2*stride);
        vec_st(v3, 0, dst+3*stride);

        dst += 4*stride;
    }
}

static void h264_idct_dc_add_altivec(uint8_t *dst, DCTELEM *block, int stride)
{
    h264_idct_dc_add_internal(dst, block, stride, 4);
}

static void ff_h264_idct8_dc_add_altivec(uint8_t *dst, DCTELEM *block, int stride)
{
    h264_idct_dc_add_internal(dst, block, stride, 8);
}

static void ff_h264_idct_add16_altivec(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) h264_idct_dc_add_altivec(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct_add_altivec(dst + block_offset[i], block + i*16, stride);
        }
    }
}

static void ff_h264_idct_add16intra_altivec(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i++){
        if(nnzc[ scan8[i] ]) ff_h264_idct_add_altivec(dst + block_offset[i], block + i*16, stride);
        else if(block[i*16]) h264_idct_dc_add_altivec(dst + block_offset[i], block + i*16, stride);
    }
}

static void ff_h264_idct8_add4_altivec(uint8_t *dst, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=0; i<16; i+=4){
        int nnz = nnzc[ scan8[i] ];
        if(nnz){
            if(nnz==1 && block[i*16]) ff_h264_idct8_dc_add_altivec(dst + block_offset[i], block + i*16, stride);
            else                      ff_h264_idct8_add_altivec   (dst + block_offset[i], block + i*16, stride);
        }
    }
}

static void ff_h264_idct_add8_altivec(uint8_t **dest, const int *block_offset, DCTELEM *block, int stride, const uint8_t nnzc[6*8]){
    int i;
    for(i=16; i<16+8; i++){
        if(nnzc[ scan8[i] ])
            ff_h264_idct_add_altivec(dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
        else if(block[i*16])
            h264_idct_dc_add_altivec(dest[(i&4)>>2] + block_offset[i], block + i*16, stride);
    }
}

#define transpose4x16(r0, r1, r2, r3) {      \
    register vec_u8 r4;                    \
    register vec_u8 r5;                    \
    register vec_u8 r6;                    \
    register vec_u8 r7;                    \
                                             \
    r4 = vec_mergeh(r0, r2);  /*0, 2 set 0*/ \
    r5 = vec_mergel(r0, r2);  /*0, 2 set 1*/ \
    r6 = vec_mergeh(r1, r3);  /*1, 3 set 0*/ \
    r7 = vec_mergel(r1, r3);  /*1, 3 set 1*/ \
                                             \
    r0 = vec_mergeh(r4, r6);  /*all set 0*/  \
    r1 = vec_mergel(r4, r6);  /*all set 1*/  \
    r2 = vec_mergeh(r5, r7);  /*all set 2*/  \
    r3 = vec_mergel(r5, r7);  /*all set 3*/  \
}

static inline void write16x4(uint8_t *dst, int dst_stride,
                             register vec_u8 r0, register vec_u8 r1,
                             register vec_u8 r2, register vec_u8 r3) {
    DECLARE_ALIGNED(16, unsigned char, result)[64];
    uint32_t *src_int = (uint32_t *)result, *dst_int = (uint32_t *)dst;
    int int_dst_stride = dst_stride/4;

    vec_st(r0, 0, result);
    vec_st(r1, 16, result);
    vec_st(r2, 32, result);
    vec_st(r3, 48, result);
    /* FIXME: there has to be a better way!!!! */
    *dst_int = *src_int;
    *(dst_int+   int_dst_stride) = *(src_int + 1);
    *(dst_int+ 2*int_dst_stride) = *(src_int + 2);
    *(dst_int+ 3*int_dst_stride) = *(src_int + 3);
    *(dst_int+ 4*int_dst_stride) = *(src_int + 4);
    *(dst_int+ 5*int_dst_stride) = *(src_int + 5);
    *(dst_int+ 6*int_dst_stride) = *(src_int + 6);
    *(dst_int+ 7*int_dst_stride) = *(src_int + 7);
    *(dst_int+ 8*int_dst_stride) = *(src_int + 8);
    *(dst_int+ 9*int_dst_stride) = *(src_int + 9);
    *(dst_int+10*int_dst_stride) = *(src_int + 10);
    *(dst_int+11*int_dst_stride) = *(src_int + 11);
    *(dst_int+12*int_dst_stride) = *(src_int + 12);
    *(dst_int+13*int_dst_stride) = *(src_int + 13);
    *(dst_int+14*int_dst_stride) = *(src_int + 14);
    *(dst_int+15*int_dst_stride) = *(src_int + 15);
}

/** \brief performs a 6x16 transpose of data in src, and stores it to dst
    \todo FIXME: see if we can't spare some vec_lvsl() by them factorizing
    out of unaligned_load() */
#define readAndTranspose16x6(src, src_stride, r8, r9, r10, r11, r12, r13) {\
    register vec_u8 r0  = unaligned_load(0,             src);            \
    register vec_u8 r1  = unaligned_load(   src_stride, src);            \
    register vec_u8 r2  = unaligned_load(2* src_stride, src);            \
    register vec_u8 r3  = unaligned_load(3* src_stride, src);            \
    register vec_u8 r4  = unaligned_load(4* src_stride, src);            \
    register vec_u8 r5  = unaligned_load(5* src_stride, src);            \
    register vec_u8 r6  = unaligned_load(6* src_stride, src);            \
    register vec_u8 r7  = unaligned_load(7* src_stride, src);            \
    register vec_u8 r14 = unaligned_load(14*src_stride, src);            \
    register vec_u8 r15 = unaligned_load(15*src_stride, src);            \
                                                                           \
    r8  = unaligned_load( 8*src_stride, src);                              \
    r9  = unaligned_load( 9*src_stride, src);                              \
    r10 = unaligned_load(10*src_stride, src);                              \
    r11 = unaligned_load(11*src_stride, src);                              \
    r12 = unaligned_load(12*src_stride, src);                              \
    r13 = unaligned_load(13*src_stride, src);                              \
                                                                           \
    /*Merge first pairs*/                                                  \
    r0 = vec_mergeh(r0, r8);    /*0, 8*/                                   \
    r1 = vec_mergeh(r1, r9);    /*1, 9*/                                   \
    r2 = vec_mergeh(r2, r10);   /*2,10*/                                   \
    r3 = vec_mergeh(r3, r11);   /*3,11*/                                   \
    r4 = vec_mergeh(r4, r12);   /*4,12*/                                   \
    r5 = vec_mergeh(r5, r13);   /*5,13*/                                   \
    r6 = vec_mergeh(r6, r14);   /*6,14*/                                   \
    r7 = vec_mergeh(r7, r15);   /*7,15*/                                   \
                                                                           \
    /*Merge second pairs*/                                                 \
    r8  = vec_mergeh(r0, r4);   /*0,4, 8,12 set 0*/                        \
    r9  = vec_mergel(r0, r4);   /*0,4, 8,12 set 1*/                        \
    r10 = vec_mergeh(r1, r5);   /*1,5, 9,13 set 0*/                        \
    r11 = vec_mergel(r1, r5);   /*1,5, 9,13 set 1*/                        \
    r12 = vec_mergeh(r2, r6);   /*2,6,10,14 set 0*/                        \
    r13 = vec_mergel(r2, r6);   /*2,6,10,14 set 1*/                        \
    r14 = vec_mergeh(r3, r7);   /*3,7,11,15 set 0*/                        \
    r15 = vec_mergel(r3, r7);   /*3,7,11,15 set 1*/                        \
                                                                           \
    /*Third merge*/                                                        \
    r0 = vec_mergeh(r8,  r12);  /*0,2,4,6,8,10,12,14 set 0*/               \
    r1 = vec_mergel(r8,  r12);  /*0,2,4,6,8,10,12,14 set 1*/               \
    r2 = vec_mergeh(r9,  r13);  /*0,2,4,6,8,10,12,14 set 2*/               \
    r4 = vec_mergeh(r10, r14);  /*1,3,5,7,9,11,13,15 set 0*/               \
    r5 = vec_mergel(r10, r14);  /*1,3,5,7,9,11,13,15 set 1*/               \
    r6 = vec_mergeh(r11, r15);  /*1,3,5,7,9,11,13,15 set 2*/               \
    /* Don't need to compute 3 and 7*/                                     \
                                                                           \
    /*Final merge*/                                                        \
    r8  = vec_mergeh(r0, r4);   /*all set 0*/                              \
    r9  = vec_mergel(r0, r4);   /*all set 1*/                              \
    r10 = vec_mergeh(r1, r5);   /*all set 2*/                              \
    r11 = vec_mergel(r1, r5);   /*all set 3*/                              \
    r12 = vec_mergeh(r2, r6);   /*all set 4*/                              \
    r13 = vec_mergel(r2, r6);   /*all set 5*/                              \
    /* Don't need to compute 14 and 15*/                                   \
                                                                           \
}

// out: o = |x-y| < a
static inline vec_u8 diff_lt_altivec ( register vec_u8 x,
                                         register vec_u8 y,
                                         register vec_u8 a) {

    register vec_u8 diff = vec_subs(x, y);
    register vec_u8 diffneg = vec_subs(y, x);
    register vec_u8 o = vec_or(diff, diffneg); /* |x-y| */
    o = (vec_u8)vec_cmplt(o, a);
    return o;
}

static inline vec_u8 h264_deblock_mask ( register vec_u8 p0,
                                           register vec_u8 p1,
                                           register vec_u8 q0,
                                           register vec_u8 q1,
                                           register vec_u8 alpha,
                                           register vec_u8 beta) {

    register vec_u8 mask;
    register vec_u8 tempmask;

    mask = diff_lt_altivec(p0, q0, alpha);
    tempmask = diff_lt_altivec(p1, p0, beta);
    mask = vec_and(mask, tempmask);
    tempmask = diff_lt_altivec(q1, q0, beta);
    mask = vec_and(mask, tempmask);

    return mask;
}

// out: newp1 = clip((p2 + ((p0 + q0 + 1) >> 1)) >> 1, p1-tc0, p1+tc0)
static inline vec_u8 h264_deblock_q1(register vec_u8 p0,
                                       register vec_u8 p1,
                                       register vec_u8 p2,
                                       register vec_u8 q0,
                                       register vec_u8 tc0) {

    register vec_u8 average = vec_avg(p0, q0);
    register vec_u8 temp;
    register vec_u8 uncliped;
    register vec_u8 ones;
    register vec_u8 max;
    register vec_u8 min;
    register vec_u8 newp1;

    temp = vec_xor(average, p2);
    average = vec_avg(average, p2);     /*avg(p2, avg(p0, q0)) */
    ones = vec_splat_u8(1);
    temp = vec_and(temp, ones);         /*(p2^avg(p0, q0)) & 1 */
    uncliped = vec_subs(average, temp); /*(p2+((p0+q0+1)>>1))>>1 */
    max = vec_adds(p1, tc0);
    min = vec_subs(p1, tc0);
    newp1 = vec_max(min, uncliped);
    newp1 = vec_min(max, newp1);
    return newp1;
}

#define h264_deblock_p0_q0(p0, p1, q0, q1, tc0masked) {                                           \
                                                                                                  \
    const vec_u8 A0v = vec_sl(vec_splat_u8(10), vec_splat_u8(4));                               \
                                                                                                  \
    register vec_u8 pq0bit = vec_xor(p0,q0);                                                    \
    register vec_u8 q1minus;                                                                    \
    register vec_u8 p0minus;                                                                    \
    register vec_u8 stage1;                                                                     \
    register vec_u8 stage2;                                                                     \
    register vec_u8 vec160;                                                                     \
    register vec_u8 delta;                                                                      \
    register vec_u8 deltaneg;                                                                   \
                                                                                                  \
    q1minus = vec_nor(q1, q1);                 /* 255 - q1 */                                     \
    stage1 = vec_avg(p1, q1minus);             /* (p1 - q1 + 256)>>1 */                           \
    stage2 = vec_sr(stage1, vec_splat_u8(1));  /* (p1 - q1 + 256)>>2 = 64 + (p1 - q1) >> 2 */     \
    p0minus = vec_nor(p0, p0);                 /* 255 - p0 */                                     \
    stage1 = vec_avg(q0, p0minus);             /* (q0 - p0 + 256)>>1 */                           \
    pq0bit = vec_and(pq0bit, vec_splat_u8(1));                                                    \
    stage2 = vec_avg(stage2, pq0bit);          /* 32 + ((q0 - p0)&1 + (p1 - q1) >> 2 + 1) >> 1 */ \
    stage2 = vec_adds(stage2, stage1);         /* 160 + ((p0 - q0) + (p1 - q1) >> 2 + 1) >> 1 */  \
    vec160 = vec_ld(0, &A0v);                                                                     \
    deltaneg = vec_subs(vec160, stage2);       /* -d */                                           \
    delta = vec_subs(stage2, vec160);          /* d */                                            \
    deltaneg = vec_min(tc0masked, deltaneg);                                                      \
    delta = vec_min(tc0masked, delta);                                                            \
    p0 = vec_subs(p0, deltaneg);                                                                  \
    q0 = vec_subs(q0, delta);                                                                     \
    p0 = vec_adds(p0, delta);                                                                     \
    q0 = vec_adds(q0, deltaneg);                                                                  \
}

#define h264_loop_filter_luma_altivec(p2, p1, p0, q0, q1, q2, alpha, beta, tc0) {            \
    DECLARE_ALIGNED(16, unsigned char, temp)[16];                                             \
    register vec_u8 alphavec;                                                              \
    register vec_u8 betavec;                                                               \
    register vec_u8 mask;                                                                  \
    register vec_u8 p1mask;                                                                \
    register vec_u8 q1mask;                                                                \
    register vector signed   char tc0vec;                                                    \
    register vec_u8 finaltc0;                                                              \
    register vec_u8 tc0masked;                                                             \
    register vec_u8 newp1;                                                                 \
    register vec_u8 newq1;                                                                 \
                                                                                             \
    temp[0] = alpha;                                                                         \
    temp[1] = beta;                                                                          \
    alphavec = vec_ld(0, temp);                                                              \
    betavec = vec_splat(alphavec, 0x1);                                                      \
    alphavec = vec_splat(alphavec, 0x0);                                                     \
    mask = h264_deblock_mask(p0, p1, q0, q1, alphavec, betavec); /*if in block */            \
                                                                                             \
    *((int *)temp) = *((int *)tc0);                                                          \
    tc0vec = vec_ld(0, (signed char*)temp);                                                  \
    tc0vec = vec_mergeh(tc0vec, tc0vec);                                                     \
    tc0vec = vec_mergeh(tc0vec, tc0vec);                                                     \
    mask = vec_and(mask, vec_cmpgt(tc0vec, vec_splat_s8(-1)));  /* if tc0[i] >= 0 */         \
    finaltc0 = vec_and((vec_u8)tc0vec, mask);     /* tc = tc0 */                           \
                                                                                             \
    p1mask = diff_lt_altivec(p2, p0, betavec);                                               \
    p1mask = vec_and(p1mask, mask);                             /* if ( |p2 - p0| < beta) */ \
    tc0masked = vec_and(p1mask, (vec_u8)tc0vec);                                           \
    finaltc0 = vec_sub(finaltc0, p1mask);                       /* tc++ */                   \
    newp1 = h264_deblock_q1(p0, p1, p2, q0, tc0masked);                                      \
    /*end if*/                                                                               \
                                                                                             \
    q1mask = diff_lt_altivec(q2, q0, betavec);                                               \
    q1mask = vec_and(q1mask, mask);                             /* if ( |q2 - q0| < beta ) */\
    tc0masked = vec_and(q1mask, (vec_u8)tc0vec);                                           \
    finaltc0 = vec_sub(finaltc0, q1mask);                       /* tc++ */                   \
    newq1 = h264_deblock_q1(p0, q1, q2, q0, tc0masked);                                      \
    /*end if*/                                                                               \
                                                                                             \
    h264_deblock_p0_q0(p0, p1, q0, q1, finaltc0);                                            \
    p1 = newp1;                                                                              \
    q1 = newq1;                                                                              \
}

static void h264_v_loop_filter_luma_altivec(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0) {

    if ((tc0[0] & tc0[1] & tc0[2] & tc0[3]) >= 0) {
        register vec_u8 p2 = vec_ld(-3*stride, pix);
        register vec_u8 p1 = vec_ld(-2*stride, pix);
        register vec_u8 p0 = vec_ld(-1*stride, pix);
        register vec_u8 q0 = vec_ld(0, pix);
        register vec_u8 q1 = vec_ld(stride, pix);
        register vec_u8 q2 = vec_ld(2*stride, pix);
        h264_loop_filter_luma_altivec(p2, p1, p0, q0, q1, q2, alpha, beta, tc0);
        vec_st(p1, -2*stride, pix);
        vec_st(p0, -1*stride, pix);
        vec_st(q0, 0, pix);
        vec_st(q1, stride, pix);
    }
}

static void h264_h_loop_filter_luma_altivec(uint8_t *pix, int stride, int alpha, int beta, int8_t *tc0) {

    register vec_u8 line0, line1, line2, line3, line4, line5;
    if ((tc0[0] & tc0[1] & tc0[2] & tc0[3]) < 0)
        return;
    readAndTranspose16x6(pix-3, stride, line0, line1, line2, line3, line4, line5);
    h264_loop_filter_luma_altivec(line0, line1, line2, line3, line4, line5, alpha, beta, tc0);
    transpose4x16(line1, line2, line3, line4);
    write16x4(pix-2, stride, line1, line2, line3, line4);
}

static av_always_inline
void weight_h264_WxH_altivec(uint8_t *block, int stride, int log2_denom, int weight, int offset, int w, int h)
{
    int y, aligned;
    vec_u8 vblock;
    vec_s16 vtemp, vweight, voffset, v0, v1;
    vec_u16 vlog2_denom;
    DECLARE_ALIGNED(16, int32_t, temp)[4];
    LOAD_ZERO;

    offset <<= log2_denom;
    if(log2_denom) offset += 1<<(log2_denom-1);
    temp[0] = log2_denom;
    temp[1] = weight;
    temp[2] = offset;

    vtemp = (vec_s16)vec_ld(0, temp);
    vlog2_denom = (vec_u16)vec_splat(vtemp, 1);
    vweight = vec_splat(vtemp, 3);
    voffset = vec_splat(vtemp, 5);
    aligned = !((unsigned long)block & 0xf);

    for (y=0; y<h; y++) {
        vblock = vec_ld(0, block);

        v0 = (vec_s16)vec_mergeh(zero_u8v, vblock);
        v1 = (vec_s16)vec_mergel(zero_u8v, vblock);

        if (w == 16 || aligned) {
            v0 = vec_mladd(v0, vweight, zero_s16v);
            v0 = vec_adds(v0, voffset);
            v0 = vec_sra(v0, vlog2_denom);
        }
        if (w == 16 || !aligned) {
            v1 = vec_mladd(v1, vweight, zero_s16v);
            v1 = vec_adds(v1, voffset);
            v1 = vec_sra(v1, vlog2_denom);
        }
        vblock = vec_packsu(v0, v1);
        vec_st(vblock, 0, block);

        block += stride;
    }
}

static av_always_inline
void biweight_h264_WxH_altivec(uint8_t *dst, uint8_t *src, int stride, int log2_denom,
                               int weightd, int weights, int offset, int w, int h)
{
    int y, dst_aligned, src_aligned;
    vec_u8 vsrc, vdst;
    vec_s16 vtemp, vweights, vweightd, voffset, v0, v1, v2, v3;
    vec_u16 vlog2_denom;
    DECLARE_ALIGNED(16, int32_t, temp)[4];
    LOAD_ZERO;

    offset = ((offset + 1) | 1) << log2_denom;
    temp[0] = log2_denom+1;
    temp[1] = weights;
    temp[2] = weightd;
    temp[3] = offset;

    vtemp = (vec_s16)vec_ld(0, temp);
    vlog2_denom = (vec_u16)vec_splat(vtemp, 1);
    vweights = vec_splat(vtemp, 3);
    vweightd = vec_splat(vtemp, 5);
    voffset = vec_splat(vtemp, 7);
    dst_aligned = !((unsigned long)dst & 0xf);
    src_aligned = !((unsigned long)src & 0xf);

    for (y=0; y<h; y++) {
        vdst = vec_ld(0, dst);
        vsrc = vec_ld(0, src);

        v0 = (vec_s16)vec_mergeh(zero_u8v, vdst);
        v1 = (vec_s16)vec_mergel(zero_u8v, vdst);
        v2 = (vec_s16)vec_mergeh(zero_u8v, vsrc);
        v3 = (vec_s16)vec_mergel(zero_u8v, vsrc);

        if (w == 8) {
            if (src_aligned)
                v3 = v2;
            else
                v2 = v3;
        }

        if (w == 16 || dst_aligned) {
            v0 = vec_mladd(v0, vweightd, zero_s16v);
            v2 = vec_mladd(v2, vweights, zero_s16v);

            v0 = vec_adds(v0, voffset);
            v0 = vec_adds(v0, v2);
            v0 = vec_sra(v0, vlog2_denom);
        }
        if (w == 16 || !dst_aligned) {
            v1 = vec_mladd(v1, vweightd, zero_s16v);
            v3 = vec_mladd(v3, vweights, zero_s16v);

            v1 = vec_adds(v1, voffset);
            v1 = vec_adds(v1, v3);
            v1 = vec_sra(v1, vlog2_denom);
        }
        vdst = vec_packsu(v0, v1);
        vec_st(vdst, 0, dst);

        dst += stride;
        src += stride;
    }
}

#define H264_WEIGHT(W,H) \
static void ff_weight_h264_pixels ## W ## x ## H ## _altivec(uint8_t *block, int stride, int log2_denom, int weight, int offset){ \
    weight_h264_WxH_altivec(block, stride, log2_denom, weight, offset, W, H); \
}\
static void ff_biweight_h264_pixels ## W ## x ## H ## _altivec(uint8_t *dst, uint8_t *src, int stride, int log2_denom, int weightd, int weights, int offset){ \
    biweight_h264_WxH_altivec(dst, src, stride, log2_denom, weightd, weights, offset, W, H); \
}

H264_WEIGHT(16,16)
H264_WEIGHT(16, 8)
H264_WEIGHT( 8,16)
H264_WEIGHT( 8, 8)
H264_WEIGHT( 8, 4)

void dsputil_h264_init_ppc(DSPContext* c, AVCodecContext *avctx) {

    if (has_altivec()) {
        c->put_h264_chroma_pixels_tab[0] = put_h264_chroma_mc8_altivec;
        c->avg_h264_chroma_pixels_tab[0] = avg_h264_chroma_mc8_altivec;
        c->put_no_rnd_vc1_chroma_pixels_tab[0] = put_no_rnd_vc1_chroma_mc8_altivec;
        c->avg_no_rnd_vc1_chroma_pixels_tab[0] = avg_no_rnd_vc1_chroma_mc8_altivec;

#define dspfunc(PFX, IDX, NUM) \
        c->PFX ## _pixels_tab[IDX][ 0] = PFX ## NUM ## _mc00_altivec; \
        c->PFX ## _pixels_tab[IDX][ 1] = PFX ## NUM ## _mc10_altivec; \
        c->PFX ## _pixels_tab[IDX][ 2] = PFX ## NUM ## _mc20_altivec; \
        c->PFX ## _pixels_tab[IDX][ 3] = PFX ## NUM ## _mc30_altivec; \
        c->PFX ## _pixels_tab[IDX][ 4] = PFX ## NUM ## _mc01_altivec; \
        c->PFX ## _pixels_tab[IDX][ 5] = PFX ## NUM ## _mc11_altivec; \
        c->PFX ## _pixels_tab[IDX][ 6] = PFX ## NUM ## _mc21_altivec; \
        c->PFX ## _pixels_tab[IDX][ 7] = PFX ## NUM ## _mc31_altivec; \
        c->PFX ## _pixels_tab[IDX][ 8] = PFX ## NUM ## _mc02_altivec; \
        c->PFX ## _pixels_tab[IDX][ 9] = PFX ## NUM ## _mc12_altivec; \
        c->PFX ## _pixels_tab[IDX][10] = PFX ## NUM ## _mc22_altivec; \
        c->PFX ## _pixels_tab[IDX][11] = PFX ## NUM ## _mc32_altivec; \
        c->PFX ## _pixels_tab[IDX][12] = PFX ## NUM ## _mc03_altivec; \
        c->PFX ## _pixels_tab[IDX][13] = PFX ## NUM ## _mc13_altivec; \
        c->PFX ## _pixels_tab[IDX][14] = PFX ## NUM ## _mc23_altivec; \
        c->PFX ## _pixels_tab[IDX][15] = PFX ## NUM ## _mc33_altivec

        dspfunc(put_h264_qpel, 0, 16);
        dspfunc(avg_h264_qpel, 0, 16);
#undef dspfunc
    }
}

void ff_h264dsp_init_ppc(H264DSPContext *c)
{
    if (has_altivec()) {
        c->h264_idct_add = ff_h264_idct_add_altivec;
        c->h264_idct_add8 = ff_h264_idct_add8_altivec;
        c->h264_idct_add16 = ff_h264_idct_add16_altivec;
        c->h264_idct_add16intra = ff_h264_idct_add16intra_altivec;
        c->h264_idct_dc_add= h264_idct_dc_add_altivec;
        c->h264_idct8_dc_add = ff_h264_idct8_dc_add_altivec;
        c->h264_idct8_add = ff_h264_idct8_add_altivec;
        c->h264_idct8_add4 = ff_h264_idct8_add4_altivec;
        c->h264_v_loop_filter_luma= h264_v_loop_filter_luma_altivec;
        c->h264_h_loop_filter_luma= h264_h_loop_filter_luma_altivec;

        c->weight_h264_pixels_tab[0] = ff_weight_h264_pixels16x16_altivec;
        c->weight_h264_pixels_tab[1] = ff_weight_h264_pixels16x8_altivec;
        c->weight_h264_pixels_tab[2] = ff_weight_h264_pixels8x16_altivec;
        c->weight_h264_pixels_tab[3] = ff_weight_h264_pixels8x8_altivec;
        c->weight_h264_pixels_tab[4] = ff_weight_h264_pixels8x4_altivec;
        c->biweight_h264_pixels_tab[0] = ff_biweight_h264_pixels16x16_altivec;
        c->biweight_h264_pixels_tab[1] = ff_biweight_h264_pixels16x8_altivec;
        c->biweight_h264_pixels_tab[2] = ff_biweight_h264_pixels8x16_altivec;
        c->biweight_h264_pixels_tab[3] = ff_biweight_h264_pixels8x8_altivec;
        c->biweight_h264_pixels_tab[4] = ff_biweight_h264_pixels8x4_altivec;
    }
}
