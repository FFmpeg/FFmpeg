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

#include "../dsputil.h"

#include "gcc_fixes.h"

#include "dsputil_altivec.h"
#include "types_altivec.h"

#define PUT_OP_U8_ALTIVEC(d, s, dst) d = s
#define AVG_OP_U8_ALTIVEC(d, s, dst) d = vec_avg(dst, s)

#define OP_U8_ALTIVEC                          PUT_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         put_h264_chroma_mc8_altivec
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
#undef PREFIX_h264_chroma_mc8_num
#undef PREFIX_h264_qpel16_h_lowpass_altivec
#undef PREFIX_h264_qpel16_h_lowpass_num
#undef PREFIX_h264_qpel16_v_lowpass_altivec
#undef PREFIX_h264_qpel16_v_lowpass_num
#undef PREFIX_h264_qpel16_hv_lowpass_altivec
#undef PREFIX_h264_qpel16_hv_lowpass_num

#define OP_U8_ALTIVEC                          AVG_OP_U8_ALTIVEC
#define PREFIX_h264_chroma_mc8_altivec         avg_h264_chroma_mc8_altivec
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
    DECLARE_ALIGNED_16(uint8_t, half[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc20_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc30_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, half[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+1, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc01_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, half[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc02_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    OPNAME ## h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(dst, src, stride, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc03_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, half[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(half, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, src+stride, half, stride, stride, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc11_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc31_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc13_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc33_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc22_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(int16_t, tmp[SIZE*(SIZE+8)]);\
    OPNAME ## h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(dst, tmp, src, stride, SIZE, stride);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc21_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfHV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(int16_t, tmp[SIZE*(SIZE+8)]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc23_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfH[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfHV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(int16_t, tmp[SIZE*(SIZE+8)]);\
    put_h264_qpel ## SIZE ## _h_lowpass_ ## CODETYPE(halfH, src + stride, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfH, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc12_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfHV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(int16_t, tmp[SIZE*(SIZE+8)]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\
\
static void OPNAME ## h264_qpel ## SIZE ## _mc32_ ## CODETYPE(uint8_t *dst, uint8_t *src, int stride){\
    DECLARE_ALIGNED_16(uint8_t, halfV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(uint8_t, halfHV[SIZE*SIZE]);\
    DECLARE_ALIGNED_16(int16_t, tmp[SIZE*(SIZE+8)]);\
    put_h264_qpel ## SIZE ## _v_lowpass_ ## CODETYPE(halfV, src+1, SIZE, stride);\
    put_h264_qpel ## SIZE ## _hv_lowpass_ ## CODETYPE(halfHV, tmp, src, SIZE, SIZE, stride);\
    OPNAME ## pixels ## SIZE ## _l2_ ## CODETYPE(dst, halfV, halfHV, stride, SIZE, SIZE);\
}\

/* this code assume that stride % 16 == 0 */
void put_no_rnd_h264_chroma_mc8_altivec(uint8_t * dst, uint8_t * src, int stride, int h, int x, int y) {
    signed int ABCD[4] __attribute__((aligned(16))) =
                        {((8 - x) * (8 - y)),
                          ((x) * (8 - y)),
                          ((8 - x) * (y)),
                          ((x) * (y))};
    register int i;
    vector unsigned char fperm;
    const vector signed int vABCD = vec_ld(0, ABCD);
    const vector signed short vA = vec_splat((vector signed short)vABCD, 1);
    const vector signed short vB = vec_splat((vector signed short)vABCD, 3);
    const vector signed short vC = vec_splat((vector signed short)vABCD, 5);
    const vector signed short vD = vec_splat((vector signed short)vABCD, 7);
    const vector signed int vzero = vec_splat_s32(0);
    const vector signed short v28ss = vec_sub(vec_sl(vec_splat_s16(1),vec_splat_u16(5)),vec_splat_s16(4));
    const vector unsigned short v6us = vec_splat_u16(6);
    register int loadSecond = (((unsigned long)src) % 16) <= 7 ? 0 : 1;
    register int reallyBadAlign = (((unsigned long)src) % 16) == 15 ? 1 : 0;

    vector unsigned char vsrcAuc, vsrcBuc, vsrcperm0, vsrcperm1;
    vector unsigned char vsrc0uc, vsrc1uc;
    vector signed short vsrc0ssH, vsrc1ssH;
    vector unsigned char vsrcCuc, vsrc2uc, vsrc3uc;
    vector signed short vsrc2ssH, vsrc3ssH, psum;
    vector unsigned char vdst, ppsum, fsum;

    if (((unsigned long)dst) % 16 == 0) {
      fperm = (vector unsigned char)AVV(0x10, 0x11, 0x12, 0x13,
                                        0x14, 0x15, 0x16, 0x17,
                                        0x08, 0x09, 0x0A, 0x0B,
                                        0x0C, 0x0D, 0x0E, 0x0F);
    } else {
      fperm = (vector unsigned char)AVV(0x00, 0x01, 0x02, 0x03,
                                        0x04, 0x05, 0x06, 0x07,
                                        0x18, 0x19, 0x1A, 0x1B,
                                        0x1C, 0x1D, 0x1E, 0x1F);
    }

    vsrcAuc = vec_ld(0, src);

    if (loadSecond)
      vsrcBuc = vec_ld(16, src);
    vsrcperm0 = vec_lvsl(0, src);
    vsrcperm1 = vec_lvsl(1, src);

    vsrc0uc = vec_perm(vsrcAuc, vsrcBuc, vsrcperm0);
    if (reallyBadAlign)
      vsrc1uc = vsrcBuc;
    else
      vsrc1uc = vec_perm(vsrcAuc, vsrcBuc, vsrcperm1);

    vsrc0ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                               (vector unsigned char)vsrc0uc);
    vsrc1ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                               (vector unsigned char)vsrc1uc);

    if (!loadSecond) {// -> !reallyBadAlign
      for (i = 0 ; i < h ; i++) {


        vsrcCuc = vec_ld(stride + 0, src);

        vsrc2uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm0);
        vsrc3uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm1);

        vsrc2ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                                (vector unsigned char)vsrc2uc);
        vsrc3ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                                (vector unsigned char)vsrc3uc);

        psum = vec_mladd(vA, vsrc0ssH, vec_splat_s16(0));
        psum = vec_mladd(vB, vsrc1ssH, psum);
        psum = vec_mladd(vC, vsrc2ssH, psum);
        psum = vec_mladd(vD, vsrc3ssH, psum);
        psum = vec_add(v28ss, psum);
        psum = vec_sra(psum, v6us);

        vdst = vec_ld(0, dst);
        ppsum = (vector unsigned char)vec_packsu(psum, psum);
        fsum = vec_perm(vdst, ppsum, fperm);

        vec_st(fsum, 0, dst);

        vsrc0ssH = vsrc2ssH;
        vsrc1ssH = vsrc3ssH;

        dst += stride;
        src += stride;
      }
    } else {
        vector unsigned char vsrcDuc;
      for (i = 0 ; i < h ; i++) {
        vsrcCuc = vec_ld(stride + 0, src);
        vsrcDuc = vec_ld(stride + 16, src);

        vsrc2uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm0);
        if (reallyBadAlign)
          vsrc3uc = vsrcDuc;
        else
          vsrc3uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm1);

        vsrc2ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                                (vector unsigned char)vsrc2uc);
        vsrc3ssH = (vector signed short)vec_mergeh((vector unsigned char)vzero,
                                                (vector unsigned char)vsrc3uc);

        psum = vec_mladd(vA, vsrc0ssH, vec_splat_s16(0));
        psum = vec_mladd(vB, vsrc1ssH, psum);
        psum = vec_mladd(vC, vsrc2ssH, psum);
        psum = vec_mladd(vD, vsrc3ssH, psum);
        psum = vec_add(v28ss, psum);
        psum = vec_sr(psum, v6us);

        vdst = vec_ld(0, dst);
        ppsum = (vector unsigned char)vec_pack(psum, psum);
        fsum = vec_perm(vdst, ppsum, fperm);

        vec_st(fsum, 0, dst);

        vsrc0ssH = vsrc2ssH;
        vsrc1ssH = vsrc3ssH;

        dst += stride;
        src += stride;
      }
    }
}

static inline void put_pixels16_l2_altivec( uint8_t * dst, const uint8_t * src1,
                                    const uint8_t * src2, int dst_stride,
                                    int src_stride1, int h)
{
    int i;
    vector unsigned char a, b, d, tmp1, tmp2, mask, mask_, edges, align;

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
    vector unsigned char a, b, d, tmp1, tmp2, mask, mask_, edges, align;

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

#define IDCT8_1D_ALTIVEC(s0, s1, s2, s3, s4, s5, s6, s7,  d0, d1, d2, d3, d4, d5, d6, d7) {\
    /*        a0  = SRC(0) + SRC(4); */ \
    vec_s16_t a0v = vec_add(s0, s4);    \
    /*        a2  = SRC(0) - SRC(4); */ \
    vec_s16_t a2v = vec_sub(s0, s4);    \
    /*        a4  =           (SRC(2)>>1) - SRC(6); */ \
    vec_s16_t a4v = vec_sub(vec_sra(s2, onev), s6);    \
    /*        a6  =           (SRC(6)>>1) + SRC(2); */ \
    vec_s16_t a6v = vec_add(vec_sra(s6, onev), s2);    \
    /*        b0  =         a0 + a6; */ \
    vec_s16_t b0v = vec_add(a0v, a6v);  \
    /*        b2  =         a2 + a4; */ \
    vec_s16_t b2v = vec_add(a2v, a4v);  \
    /*        b4  =         a2 - a4; */ \
    vec_s16_t b4v = vec_sub(a2v, a4v);  \
    /*        b6  =         a0 - a6; */ \
    vec_s16_t b6v = vec_sub(a0v, a6v);  \
    /* a1 =  SRC(5) - SRC(3) - SRC(7) - (SRC(7)>>1); */ \
    /*        a1 =             (SRC(5)-SRC(3)) -  (SRC(7)  +  (SRC(7)>>1)); */ \
    vec_s16_t a1v = vec_sub( vec_sub(s5, s3), vec_add(s7, vec_sra(s7, onev)) ); \
    /* a3 =  SRC(7) + SRC(1) - SRC(3) - (SRC(3)>>1); */ \
    /*        a3 =             (SRC(7)+SRC(1)) -  (SRC(3)  +  (SRC(3)>>1)); */ \
    vec_s16_t a3v = vec_sub( vec_add(s7, s1), vec_add(s3, vec_sra(s3, onev)) );\
    /* a5 =  SRC(7) - SRC(1) + SRC(5) + (SRC(5)>>1); */ \
    /*        a5 =             (SRC(7)-SRC(1)) +   SRC(5) +   (SRC(5)>>1); */ \
    vec_s16_t a5v = vec_add( vec_sub(s7, s1), vec_add(s5, vec_sra(s5, onev)) );\
    /*        a7 =                SRC(5)+SRC(3) +  SRC(1) +   (SRC(1)>>1); */ \
    vec_s16_t a7v = vec_add( vec_add(s5, s3), vec_add(s1, vec_sra(s1, onev)) );\
    /*        b1 =                  (a7>>2)  +  a1; */ \
    vec_s16_t b1v = vec_add( vec_sra(a7v, twov), a1v); \
    /*        b3 =          a3 +        (a5>>2); */ \
    vec_s16_t b3v = vec_add(a3v, vec_sra(a5v, twov)); \
    /*        b5 =                  (a3>>2)  -   a5; */ \
    vec_s16_t b5v = vec_sub( vec_sra(a3v, twov), a5v); \
    /*        b7 =           a7 -        (a1>>2); */ \
    vec_s16_t b7v = vec_sub( a7v, vec_sra(a1v, twov)); \
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
    vec_u8_t hv = vec_ld( 0, dest );                           \
    vec_u8_t lv = vec_ld( 7, dest );                           \
    vec_u8_t dstv   = vec_perm( hv, lv, (vec_u8_t)perm_ldv );  \
    vec_s16_t idct_sh6 = vec_sra(idctv, sixv);                 \
    vec_u16_t dst16 = (vec_u16_t)vec_mergeh(zero_u8v, dstv);   \
    vec_s16_t idstsum = vec_adds(idct_sh6, (vec_s16_t)dst16);  \
    vec_u8_t idstsum8 = vec_packsu(zero_s16v, idstsum);        \
    vec_u8_t edgehv;                                           \
    /* unaligned store */                                      \
    vec_u8_t bodyv  = vec_perm( idstsum8, idstsum8, perm_stv );\
    vec_u8_t edgelv = vec_perm( sel, zero_u8v, perm_stv );     \
    lv    = vec_sel( lv, bodyv, edgelv );                      \
    vec_st( lv, 7, dest );                                     \
    hv    = vec_ld( 0, dest );                                 \
    edgehv = vec_perm( zero_u8v, sel, perm_stv );              \
    hv    = vec_sel( hv, bodyv, edgehv );                      \
    vec_st( hv, 0, dest );                                     \
 }

void ff_h264_idct8_add_altivec( uint8_t *dst, DCTELEM *dct, int stride ) {
    vec_s16_t s0, s1, s2, s3, s4, s5, s6, s7;
    vec_s16_t d0, d1, d2, d3, d4, d5, d6, d7;
    vec_s16_t idct0, idct1, idct2, idct3, idct4, idct5, idct6, idct7;

    vec_u8_t perm_ldv = vec_lvsl(0, dst);
    vec_u8_t perm_stv = vec_lvsr(8, dst);

    const vec_u16_t onev = vec_splat_u16(1);
    const vec_u16_t twov = vec_splat_u16(2);
    const vec_u16_t sixv = vec_splat_u16(6);

    const vec_u8_t sel = (vec_u8_t) AVV(0,0,0,0,0,0,0,0,
                                        -1,-1,-1,-1,-1,-1,-1,-1);
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

void dsputil_h264_init_ppc(DSPContext* c, AVCodecContext *avctx) {

#ifdef HAVE_ALTIVEC
  if (has_altivec()) {
    c->put_h264_chroma_pixels_tab[0] = put_h264_chroma_mc8_altivec;
    c->put_no_rnd_h264_chroma_pixels_tab[0] = put_no_rnd_h264_chroma_mc8_altivec;
    c->avg_h264_chroma_pixels_tab[0] = avg_h264_chroma_mc8_altivec;
    c->h264_idct8_add = ff_h264_idct8_add_altivec;

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

  } else
#endif /* HAVE_ALTIVEC */
  {
    // Non-AltiVec PPC optimisations

    // ... pending ...
  }
}
