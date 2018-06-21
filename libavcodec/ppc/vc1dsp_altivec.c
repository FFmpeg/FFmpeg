/*
 * VC-1 and WMV3 decoder - DSP functions AltiVec-optimized
 * Copyright (c) 2006 Konstantin Shishkov
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

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/vc1dsp.h"

#if HAVE_ALTIVEC

// main steps of 8x8 transform
#define STEP8(s0, s1, s2, s3, s4, s5, s6, s7, vec_rnd) \
do { \
    t0 = vec_sl(vec_add(s0, s4), vec_2); \
    t0 = vec_add(vec_sl(t0, vec_1), t0); \
    t0 = vec_add(t0, vec_rnd); \
    t1 = vec_sl(vec_sub(s0, s4), vec_2); \
    t1 = vec_add(vec_sl(t1, vec_1), t1); \
    t1 = vec_add(t1, vec_rnd); \
    t2 = vec_add(vec_sl(s6, vec_2), vec_sl(s6, vec_1)); \
    t2 = vec_add(t2, vec_sl(s2, vec_4)); \
    t3 = vec_add(vec_sl(s2, vec_2), vec_sl(s2, vec_1)); \
    t3 = vec_sub(t3, vec_sl(s6, vec_4)); \
    t4 = vec_add(t0, t2); \
    t5 = vec_add(t1, t3); \
    t6 = vec_sub(t1, t3); \
    t7 = vec_sub(t0, t2); \
\
    t0 = vec_sl(vec_add(s1, s3), vec_4); \
    t0 = vec_add(t0, vec_sl(s5, vec_3)); \
    t0 = vec_add(t0, vec_sl(s7, vec_2)); \
    t0 = vec_add(t0, vec_sub(s5, s3)); \
\
    t1 = vec_sl(vec_sub(s1, s5), vec_4); \
    t1 = vec_sub(t1, vec_sl(s7, vec_3)); \
    t1 = vec_sub(t1, vec_sl(s3, vec_2)); \
    t1 = vec_sub(t1, vec_add(s1, s7)); \
\
    t2 = vec_sl(vec_sub(s7, s3), vec_4); \
    t2 = vec_add(t2, vec_sl(s1, vec_3)); \
    t2 = vec_add(t2, vec_sl(s5, vec_2)); \
    t2 = vec_add(t2, vec_sub(s1, s7)); \
\
    t3 = vec_sl(vec_sub(s5, s7), vec_4); \
    t3 = vec_sub(t3, vec_sl(s3, vec_3)); \
    t3 = vec_add(t3, vec_sl(s1, vec_2)); \
    t3 = vec_sub(t3, vec_add(s3, s5)); \
\
    s0 = vec_add(t4, t0); \
    s1 = vec_add(t5, t1); \
    s2 = vec_add(t6, t2); \
    s3 = vec_add(t7, t3); \
    s4 = vec_sub(t7, t3); \
    s5 = vec_sub(t6, t2); \
    s6 = vec_sub(t5, t1); \
    s7 = vec_sub(t4, t0); \
}while(0)

#define SHIFT_HOR8(s0, s1, s2, s3, s4, s5, s6, s7) \
do { \
    s0 = vec_sra(s0, vec_3); \
    s1 = vec_sra(s1, vec_3); \
    s2 = vec_sra(s2, vec_3); \
    s3 = vec_sra(s3, vec_3); \
    s4 = vec_sra(s4, vec_3); \
    s5 = vec_sra(s5, vec_3); \
    s6 = vec_sra(s6, vec_3); \
    s7 = vec_sra(s7, vec_3); \
}while(0)

#define SHIFT_VERT8(s0, s1, s2, s3, s4, s5, s6, s7) \
do { \
    s0 = vec_sra(s0, vec_7); \
    s1 = vec_sra(s1, vec_7); \
    s2 = vec_sra(s2, vec_7); \
    s3 = vec_sra(s3, vec_7); \
    s4 = vec_sra(vec_add(s4, vec_1s), vec_7); \
    s5 = vec_sra(vec_add(s5, vec_1s), vec_7); \
    s6 = vec_sra(vec_add(s6, vec_1s), vec_7); \
    s7 = vec_sra(vec_add(s7, vec_1s), vec_7); \
}while(0)

/* main steps of 4x4 transform */
#define STEP4(s0, s1, s2, s3, vec_rnd) \
do { \
    t1 = vec_add(vec_sl(s0, vec_4), s0); \
    t1 = vec_add(t1, vec_rnd); \
    t2 = vec_add(vec_sl(s2, vec_4), s2); \
    t0 = vec_add(t1, t2); \
    t1 = vec_sub(t1, t2); \
    t3 = vec_sl(vec_sub(s3, s1), vec_1); \
    t3 = vec_add(t3, vec_sl(t3, vec_2)); \
    t2 = vec_add(t3, vec_sl(s1, vec_5)); \
    t3 = vec_add(t3, vec_sl(s3, vec_3)); \
    t3 = vec_add(t3, vec_sl(s3, vec_2)); \
    s0 = vec_add(t0, t2); \
    s1 = vec_sub(t1, t3); \
    s2 = vec_add(t1, t3); \
    s3 = vec_sub(t0, t2); \
}while (0)

#define SHIFT_HOR4(s0, s1, s2, s3) \
    s0 = vec_sra(s0, vec_3); \
    s1 = vec_sra(s1, vec_3); \
    s2 = vec_sra(s2, vec_3); \
    s3 = vec_sra(s3, vec_3);

#define SHIFT_VERT4(s0, s1, s2, s3) \
    s0 = vec_sra(s0, vec_7); \
    s1 = vec_sra(s1, vec_7); \
    s2 = vec_sra(s2, vec_7); \
    s3 = vec_sra(s3, vec_7);

/** Do inverse transform on 8x8 block
*/
static void vc1_inv_trans_8x8_altivec(int16_t block[64])
{
    vector signed short src0, src1, src2, src3, src4, src5, src6, src7;
    vector signed int s0, s1, s2, s3, s4, s5, s6, s7;
    vector signed int s8, s9, sA, sB, sC, sD, sE, sF;
    vector signed int t0, t1, t2, t3, t4, t5, t6, t7;
    const vector signed int vec_64 = vec_sl(vec_splat_s32(4), vec_splat_u32(4));
    const vector unsigned int vec_7 = vec_splat_u32(7);
    const vector unsigned int vec_4 = vec_splat_u32(4);
    const vector  signed int vec_4s = vec_splat_s32(4);
    const vector unsigned int vec_3 = vec_splat_u32(3);
    const vector unsigned int vec_2 = vec_splat_u32(2);
    const vector  signed int vec_1s = vec_splat_s32(1);
    const vector unsigned int vec_1 = vec_splat_u32(1);

    src0 = vec_ld(  0, block);
    src1 = vec_ld( 16, block);
    src2 = vec_ld( 32, block);
    src3 = vec_ld( 48, block);
    src4 = vec_ld( 64, block);
    src5 = vec_ld( 80, block);
    src6 = vec_ld( 96, block);
    src7 = vec_ld(112, block);

    s0 = vec_unpackl(src0);
    s1 = vec_unpackl(src1);
    s2 = vec_unpackl(src2);
    s3 = vec_unpackl(src3);
    s4 = vec_unpackl(src4);
    s5 = vec_unpackl(src5);
    s6 = vec_unpackl(src6);
    s7 = vec_unpackl(src7);
    s8 = vec_unpackh(src0);
    s9 = vec_unpackh(src1);
    sA = vec_unpackh(src2);
    sB = vec_unpackh(src3);
    sC = vec_unpackh(src4);
    sD = vec_unpackh(src5);
    sE = vec_unpackh(src6);
    sF = vec_unpackh(src7);
    STEP8(s0, s1, s2, s3, s4, s5, s6, s7, vec_4s);
    SHIFT_HOR8(s0, s1, s2, s3, s4, s5, s6, s7);
    STEP8(s8, s9, sA, sB, sC, sD, sE, sF, vec_4s);
    SHIFT_HOR8(s8, s9, sA, sB, sC, sD, sE, sF);
    src0 = vec_pack(s8, s0);
    src1 = vec_pack(s9, s1);
    src2 = vec_pack(sA, s2);
    src3 = vec_pack(sB, s3);
    src4 = vec_pack(sC, s4);
    src5 = vec_pack(sD, s5);
    src6 = vec_pack(sE, s6);
    src7 = vec_pack(sF, s7);
    TRANSPOSE8(src0, src1, src2, src3, src4, src5, src6, src7);

    s0 = vec_unpackl(src0);
    s1 = vec_unpackl(src1);
    s2 = vec_unpackl(src2);
    s3 = vec_unpackl(src3);
    s4 = vec_unpackl(src4);
    s5 = vec_unpackl(src5);
    s6 = vec_unpackl(src6);
    s7 = vec_unpackl(src7);
    s8 = vec_unpackh(src0);
    s9 = vec_unpackh(src1);
    sA = vec_unpackh(src2);
    sB = vec_unpackh(src3);
    sC = vec_unpackh(src4);
    sD = vec_unpackh(src5);
    sE = vec_unpackh(src6);
    sF = vec_unpackh(src7);
    STEP8(s0, s1, s2, s3, s4, s5, s6, s7, vec_64);
    SHIFT_VERT8(s0, s1, s2, s3, s4, s5, s6, s7);
    STEP8(s8, s9, sA, sB, sC, sD, sE, sF, vec_64);
    SHIFT_VERT8(s8, s9, sA, sB, sC, sD, sE, sF);
    src0 = vec_pack(s8, s0);
    src1 = vec_pack(s9, s1);
    src2 = vec_pack(sA, s2);
    src3 = vec_pack(sB, s3);
    src4 = vec_pack(sC, s4);
    src5 = vec_pack(sD, s5);
    src6 = vec_pack(sE, s6);
    src7 = vec_pack(sF, s7);

    vec_st(src0,  0, block);
    vec_st(src1, 16, block);
    vec_st(src2, 32, block);
    vec_st(src3, 48, block);
    vec_st(src4, 64, block);
    vec_st(src5, 80, block);
    vec_st(src6, 96, block);
    vec_st(src7,112, block);
}

/** Do inverse transform on 8x4 part of block
*/
static void vc1_inv_trans_8x4_altivec(uint8_t *dest, ptrdiff_t stride,
                                      int16_t *block)
{
    vector signed short src0, src1, src2, src3, src4, src5, src6, src7;
    vector signed int s0, s1, s2, s3, s4, s5, s6, s7;
    vector signed int s8, s9, sA, sB, sC, sD, sE, sF;
    vector signed int t0, t1, t2, t3, t4, t5, t6, t7;
    const vector signed int vec_64 = vec_sl(vec_splat_s32(4), vec_splat_u32(4));
    const vector unsigned int vec_7 = vec_splat_u32(7);
    const vector unsigned int vec_5 = vec_splat_u32(5);
    const vector unsigned int vec_4 = vec_splat_u32(4);
    const vector  signed int vec_4s = vec_splat_s32(4);
    const vector unsigned int vec_3 = vec_splat_u32(3);
    const vector unsigned int vec_2 = vec_splat_u32(2);
    const vector unsigned int vec_1 = vec_splat_u32(1);
    vector unsigned char tmp;
    vector signed short tmp2, tmp3;
    vector unsigned char perm0, perm1, p0, p1, p;

    src0 = vec_ld(  0, block);
    src1 = vec_ld( 16, block);
    src2 = vec_ld( 32, block);
    src3 = vec_ld( 48, block);
    src4 = vec_ld( 64, block);
    src5 = vec_ld( 80, block);
    src6 = vec_ld( 96, block);
    src7 = vec_ld(112, block);

    TRANSPOSE8(src0, src1, src2, src3, src4, src5, src6, src7);
    s0 = vec_unpackl(src0);
    s1 = vec_unpackl(src1);
    s2 = vec_unpackl(src2);
    s3 = vec_unpackl(src3);
    s4 = vec_unpackl(src4);
    s5 = vec_unpackl(src5);
    s6 = vec_unpackl(src6);
    s7 = vec_unpackl(src7);
    s8 = vec_unpackh(src0);
    s9 = vec_unpackh(src1);
    sA = vec_unpackh(src2);
    sB = vec_unpackh(src3);
    sC = vec_unpackh(src4);
    sD = vec_unpackh(src5);
    sE = vec_unpackh(src6);
    sF = vec_unpackh(src7);
    STEP8(s0, s1, s2, s3, s4, s5, s6, s7, vec_4s);
    SHIFT_HOR8(s0, s1, s2, s3, s4, s5, s6, s7);
    STEP8(s8, s9, sA, sB, sC, sD, sE, sF, vec_4s);
    SHIFT_HOR8(s8, s9, sA, sB, sC, sD, sE, sF);
    src0 = vec_pack(s8, s0);
    src1 = vec_pack(s9, s1);
    src2 = vec_pack(sA, s2);
    src3 = vec_pack(sB, s3);
    src4 = vec_pack(sC, s4);
    src5 = vec_pack(sD, s5);
    src6 = vec_pack(sE, s6);
    src7 = vec_pack(sF, s7);
    TRANSPOSE8(src0, src1, src2, src3, src4, src5, src6, src7);

    s0 = vec_unpackh(src0);
    s1 = vec_unpackh(src1);
    s2 = vec_unpackh(src2);
    s3 = vec_unpackh(src3);
    s8 = vec_unpackl(src0);
    s9 = vec_unpackl(src1);
    sA = vec_unpackl(src2);
    sB = vec_unpackl(src3);
    STEP4(s0, s1, s2, s3, vec_64);
    SHIFT_VERT4(s0, s1, s2, s3);
    STEP4(s8, s9, sA, sB, vec_64);
    SHIFT_VERT4(s8, s9, sA, sB);
    src0 = vec_pack(s0, s8);
    src1 = vec_pack(s1, s9);
    src2 = vec_pack(s2, sA);
    src3 = vec_pack(s3, sB);

#if HAVE_BIGENDIAN
    p0 = vec_lvsl (0, dest);
    p1 = vec_lvsl (stride, dest);
    p = vec_splat_u8 (-1);
    perm0 = vec_mergeh (p, p0);
    perm1 = vec_mergeh (p, p1);
#define GET_TMP2(dst, p)        \
    tmp = vec_ld (0, dest);     \
    tmp2 = (vector signed short)vec_perm (tmp, vec_splat_u8(0), p);
#else
#define GET_TMP2(dst,p)         \
    tmp = vec_vsx_ld (0, dst);  \
    tmp2 = (vector signed short)vec_mergeh (tmp, vec_splat_u8(0));
#endif

#define ADD(dest,src,perm)                                              \
    GET_TMP2(dest, perm);                                               \
    tmp3 = vec_adds (tmp2, src);                                        \
    tmp = vec_packsu (tmp3, tmp3);                                      \
    vec_ste ((vector unsigned int)tmp, 0, (unsigned int *)dest);        \
    vec_ste ((vector unsigned int)tmp, 4, (unsigned int *)dest);

    ADD (dest, src0, perm0)      dest += stride;
    ADD (dest, src1, perm1)      dest += stride;
    ADD (dest, src2, perm0)      dest += stride;
    ADD (dest, src3, perm1)
}

#define PUT_OP_U8_ALTIVEC(d, s, dst) d = s
#define AVG_OP_U8_ALTIVEC(d, s, dst) d = vec_avg(dst, s)

#define OP_U8_ALTIVEC                          PUT_OP_U8_ALTIVEC
#define PREFIX_no_rnd_vc1_chroma_mc8_altivec   put_no_rnd_vc1_chroma_mc8_altivec
#include "h264chroma_template.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_no_rnd_vc1_chroma_mc8_altivec

#define OP_U8_ALTIVEC                          AVG_OP_U8_ALTIVEC
#define PREFIX_no_rnd_vc1_chroma_mc8_altivec   avg_no_rnd_vc1_chroma_mc8_altivec
#include "h264chroma_template.c"
#undef OP_U8_ALTIVEC
#undef PREFIX_no_rnd_vc1_chroma_mc8_altivec

#endif /* HAVE_ALTIVEC */

av_cold void ff_vc1dsp_init_ppc(VC1DSPContext *dsp)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    dsp->vc1_inv_trans_8x8 = vc1_inv_trans_8x8_altivec;
    dsp->vc1_inv_trans_8x4 = vc1_inv_trans_8x4_altivec;
    dsp->put_no_rnd_vc1_chroma_pixels_tab[0] = put_no_rnd_vc1_chroma_mc8_altivec;
    dsp->avg_no_rnd_vc1_chroma_pixels_tab[0] = avg_no_rnd_vc1_chroma_mc8_altivec;
#endif /* HAVE_ALTIVEC */
}
