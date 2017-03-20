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

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/ppc/types_altivec.h"
#include "libavutil/ppc/util_altivec.h"

#define ASSERT_ALIGNED(ptr) av_assert2(!((uintptr_t)ptr&0x0000000F));

#if HAVE_BIGENDIAN
#define load_alignment(s, ali, pm2, pm1, pp0, pp1, pp2, pp3){\
    vec_u8 srcR1 = vec_ld(-2, s);\
    vec_u8 srcR2 = vec_ld(14, s);\
    switch (ali) {\
    default: {\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = vec_perm(srcR1, srcR2, pm1);\
        srcP0 = vec_perm(srcR1, srcR2, pp0);\
        srcP1 = vec_perm(srcR1, srcR2, pp1);\
        srcP2 = vec_perm(srcR1, srcR2, pp2);\
        srcP3 = vec_perm(srcR1, srcR2, pp3);\
    } break;\
    case 11: {\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = vec_perm(srcR1, srcR2, pm1);\
        srcP0 = vec_perm(srcR1, srcR2, pp0);\
        srcP1 = vec_perm(srcR1, srcR2, pp1);\
        srcP2 = vec_perm(srcR1, srcR2, pp2);\
        srcP3 = srcR2;\
    } break;\
    case 12: {\
        vec_u8 srcR3 = vec_ld(30, s);\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = vec_perm(srcR1, srcR2, pm1);\
        srcP0 = vec_perm(srcR1, srcR2, pp0);\
        srcP1 = vec_perm(srcR1, srcR2, pp1);\
        srcP2 = srcR2;\
        srcP3 = vec_perm(srcR2, srcR3, pp3);\
    } break;\
    case 13: {\
        vec_u8 srcR3 = vec_ld(30, s);\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = vec_perm(srcR1, srcR2, pm1);\
        srcP0 = vec_perm(srcR1, srcR2, pp0);\
        srcP1 = srcR2;\
        srcP2 = vec_perm(srcR2, srcR3, pp2);\
        srcP3 = vec_perm(srcR2, srcR3, pp3);\
    } break;\
    case 14: {\
        vec_u8 srcR3 = vec_ld(30, s);\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = vec_perm(srcR1, srcR2, pm1);\
        srcP0 = srcR2;\
        srcP1 = vec_perm(srcR2, srcR3, pp1);\
        srcP2 = vec_perm(srcR2, srcR3, pp2);\
        srcP3 = vec_perm(srcR2, srcR3, pp3);\
    } break;\
    case 15: {\
        vec_u8 srcR3 = vec_ld(30, s);\
        srcM2 = vec_perm(srcR1, srcR2, pm2);\
        srcM1 = srcR2;\
        srcP0 = vec_perm(srcR2, srcR3, pp0);\
        srcP1 = vec_perm(srcR2, srcR3, pp1);\
        srcP2 = vec_perm(srcR2, srcR3, pp2);\
        srcP3 = vec_perm(srcR2, srcR3, pp3);\
    } break;\
    }\
 }
#else
#define load_alignment(s, ali, pm2, pm1, pp0, pp1, pp2, pp3){\
    srcM2 =  vec_vsx_ld(-2, s);\
    srcM1 = vec_vsx_ld(-1, s);\
    srcP0 = vec_vsx_ld(0, s);\
    srcP1 = vec_vsx_ld(1, s);\
    srcP2 = vec_vsx_ld(2, s);\
    srcP3 = vec_vsx_ld(3, s);\
 }
#endif /* HAVE_BIGENDIAN */

/* this code assume stride % 16 == 0 */
#ifdef PREFIX_h264_qpel16_h_lowpass_altivec
static void PREFIX_h264_qpel16_h_lowpass_altivec(uint8_t *dst,
                                                 const uint8_t *src,
                                                 int dstStride, int srcStride)
{
    register int i;

    LOAD_ZERO;
    vec_u8 permM2, permM1, permP0, permP1, permP2, permP3;
    const vec_s16 v5ss = vec_splat_s16(5);
    const vec_u16 v5us = vec_splat_u16(5);
    const vec_s16 v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
    const vec_s16 v16ss = vec_sl(vec_splat_s16(1),vec_splat_u16(4));

    vec_u8 srcM2, srcM1, srcP0, srcP1, srcP2, srcP3;

    register int align = ((((unsigned long)src) - 2) % 16);

    vec_s16 srcP0A, srcP0B, srcP1A, srcP1B,
              srcP2A, srcP2B, srcP3A, srcP3B,
              srcM1A, srcM1B, srcM2A, srcM2B,
              sum1A, sum1B, sum2A, sum2B, sum3A, sum3B,
              pp1A, pp1B, pp2A, pp2B, pp3A, pp3B,
              psumA, psumB, sumA, sumB;

    vec_u8 sum, fsum;

#if HAVE_BIGENDIAN
    permM2 = vec_lvsl(-2, src);
    permM1 = vec_lvsl(-1, src);
    permP0 = vec_lvsl(+0, src);
    permP1 = vec_lvsl(+1, src);
    permP2 = vec_lvsl(+2, src);
    permP3 = vec_lvsl(+3, src);
#endif /* HAVE_BIGENDIAN */

    for (i = 0 ; i < 16 ; i ++) {
        load_alignment(src, align, permM2, permM1, permP0, permP1, permP2, permP3);

        srcP0A = (vec_s16) VEC_MERGEH(zero_u8v, srcP0);
        srcP0B = (vec_s16) VEC_MERGEL(zero_u8v, srcP0);
        srcP1A = (vec_s16) VEC_MERGEH(zero_u8v, srcP1);
        srcP1B = (vec_s16) VEC_MERGEL(zero_u8v, srcP1);

        srcP2A = (vec_s16) VEC_MERGEH(zero_u8v, srcP2);
        srcP2B = (vec_s16) VEC_MERGEL(zero_u8v, srcP2);
        srcP3A = (vec_s16) VEC_MERGEH(zero_u8v, srcP3);
        srcP3B = (vec_s16) VEC_MERGEL(zero_u8v, srcP3);

        srcM1A = (vec_s16) VEC_MERGEH(zero_u8v, srcM1);
        srcM1B = (vec_s16) VEC_MERGEL(zero_u8v, srcM1);
        srcM2A = (vec_s16) VEC_MERGEH(zero_u8v, srcM2);
        srcM2B = (vec_s16) VEC_MERGEL(zero_u8v, srcM2);

        sum1A = vec_adds(srcP0A, srcP1A);
        sum1B = vec_adds(srcP0B, srcP1B);
        sum2A = vec_adds(srcM1A, srcP2A);
        sum2B = vec_adds(srcM1B, srcP2B);
        sum3A = vec_adds(srcM2A, srcP3A);
        sum3B = vec_adds(srcM2B, srcP3B);

        pp1A = vec_mladd(sum1A, v20ss, v16ss);
        pp1B = vec_mladd(sum1B, v20ss, v16ss);

        pp2A = vec_mladd(sum2A, v5ss, zero_s16v);
        pp2B = vec_mladd(sum2B, v5ss, zero_s16v);

        pp3A = vec_add(sum3A, pp1A);
        pp3B = vec_add(sum3B, pp1B);

        psumA = vec_sub(pp3A, pp2A);
        psumB = vec_sub(pp3B, pp2B);

        sumA = vec_sra(psumA, v5us);
        sumB = vec_sra(psumB, v5us);

        sum = vec_packsu(sumA, sumB);

        ASSERT_ALIGNED(dst);

        OP_U8_ALTIVEC(fsum, sum, vec_ld(0, dst));

        vec_st(fsum, 0, dst);

        src += srcStride;
        dst += dstStride;
    }
}
#endif /* PREFIX_h264_qpel16_h_lowpass_altivec */

/* this code assume stride % 16 == 0 */
#ifdef PREFIX_h264_qpel16_v_lowpass_altivec
static void PREFIX_h264_qpel16_v_lowpass_altivec(uint8_t *dst,
                                                 const uint8_t *src,
                                                 int dstStride, int srcStride)
{
    register int i;

    LOAD_ZERO;
    vec_u8 perm;
#if HAVE_BIGENDIAN
    perm = vec_lvsl(0, src);
#endif
    const vec_s16 v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
    const vec_u16 v5us = vec_splat_u16(5);
    const vec_s16 v5ss = vec_splat_s16(5);
    const vec_s16 v16ss = vec_sl(vec_splat_s16(1),vec_splat_u16(4));

    const uint8_t *srcbis = src - (srcStride * 2);

    const vec_u8 srcM2 = load_with_perm_vec(0, srcbis, perm);
    srcbis += srcStride;
    const vec_u8 srcM1 = load_with_perm_vec(0, srcbis, perm);
    srcbis += srcStride;
    const vec_u8 srcP0 = load_with_perm_vec(0, srcbis, perm);
    srcbis += srcStride;
    const vec_u8 srcP1 = load_with_perm_vec(0, srcbis, perm);
    srcbis += srcStride;
    const vec_u8 srcP2 = load_with_perm_vec(0, srcbis, perm);
    srcbis += srcStride;

    vec_s16 srcM2ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcM2);
    vec_s16 srcM2ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcM2);
    vec_s16 srcM1ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcM1);
    vec_s16 srcM1ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcM1);
    vec_s16 srcP0ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcP0);
    vec_s16 srcP0ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcP0);
    vec_s16 srcP1ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcP1);
    vec_s16 srcP1ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcP1);
    vec_s16 srcP2ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcP2);
    vec_s16 srcP2ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcP2);

    vec_s16 pp1A, pp1B, pp2A, pp2B, pp3A, pp3B,
              psumA, psumB, sumA, sumB,
              srcP3ssA, srcP3ssB,
              sum1A, sum1B, sum2A, sum2B, sum3A, sum3B;

    vec_u8 sum, fsum, srcP3;

    for (i = 0 ; i < 16 ; i++) {
        srcP3 = load_with_perm_vec(0, srcbis, perm);
        srcbis += srcStride;

        srcP3ssA = (vec_s16) VEC_MERGEH(zero_u8v, srcP3);
        srcP3ssB = (vec_s16) VEC_MERGEL(zero_u8v, srcP3);

        sum1A = vec_adds(srcP0ssA, srcP1ssA);
        sum1B = vec_adds(srcP0ssB, srcP1ssB);
        sum2A = vec_adds(srcM1ssA, srcP2ssA);
        sum2B = vec_adds(srcM1ssB, srcP2ssB);
        sum3A = vec_adds(srcM2ssA, srcP3ssA);
        sum3B = vec_adds(srcM2ssB, srcP3ssB);

        srcM2ssA = srcM1ssA;
        srcM2ssB = srcM1ssB;
        srcM1ssA = srcP0ssA;
        srcM1ssB = srcP0ssB;
        srcP0ssA = srcP1ssA;
        srcP0ssB = srcP1ssB;
        srcP1ssA = srcP2ssA;
        srcP1ssB = srcP2ssB;
        srcP2ssA = srcP3ssA;
        srcP2ssB = srcP3ssB;

        pp1A = vec_mladd(sum1A, v20ss, v16ss);
        pp1B = vec_mladd(sum1B, v20ss, v16ss);

        pp2A = vec_mladd(sum2A, v5ss, zero_s16v);
        pp2B = vec_mladd(sum2B, v5ss, zero_s16v);

        pp3A = vec_add(sum3A, pp1A);
        pp3B = vec_add(sum3B, pp1B);

        psumA = vec_sub(pp3A, pp2A);
        psumB = vec_sub(pp3B, pp2B);

        sumA = vec_sra(psumA, v5us);
        sumB = vec_sra(psumB, v5us);

        sum = vec_packsu(sumA, sumB);

        ASSERT_ALIGNED(dst);

        OP_U8_ALTIVEC(fsum, sum, vec_ld(0, dst));

        vec_st(fsum, 0, dst);

        dst += dstStride;
    }
}
#endif /* PREFIX_h264_qpel16_v_lowpass_altivec */

/* this code assume stride % 16 == 0 *and* tmp is properly aligned */
#ifdef PREFIX_h264_qpel16_hv_lowpass_altivec
static void PREFIX_h264_qpel16_hv_lowpass_altivec(uint8_t *dst, int16_t *tmp,
                                                  const uint8_t *src,
                                                  int dstStride, int tmpStride,
                                                  int srcStride)
{
    register int i;
    LOAD_ZERO;
    vec_u8 permM2, permM1, permP0, permP1, permP2, permP3;
    const vec_s16 v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
    const vec_u32 v10ui = vec_splat_u32(10);
    const vec_s16 v5ss = vec_splat_s16(5);
    const vec_s16 v1ss = vec_splat_s16(1);
    const vec_s32 v512si = vec_sl(vec_splat_s32(1),vec_splat_u32(9));
    const vec_u32 v16ui = vec_sl(vec_splat_u32(1),vec_splat_u32(4));

    register int align = ((((unsigned long)src) - 2) % 16);

    vec_s16 srcP0A, srcP0B, srcP1A, srcP1B,
              srcP2A, srcP2B, srcP3A, srcP3B,
              srcM1A, srcM1B, srcM2A, srcM2B,
              sum1A, sum1B, sum2A, sum2B, sum3A, sum3B,
              pp1A, pp1B, pp2A, pp2B, psumA, psumB;

    const vec_u8 mperm = (const vec_u8)
        {0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
         0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F};
    int16_t *tmpbis = tmp;

    vec_s16 tmpM1ssA, tmpM1ssB, tmpM2ssA, tmpM2ssB,
              tmpP0ssA, tmpP0ssB, tmpP1ssA, tmpP1ssB,
              tmpP2ssA, tmpP2ssB;

    vec_s32 pp1Ae, pp1Ao, pp1Be, pp1Bo, pp2Ae, pp2Ao, pp2Be, pp2Bo,
              pp3Ae, pp3Ao, pp3Be, pp3Bo, pp1cAe, pp1cAo, pp1cBe, pp1cBo,
              pp32Ae, pp32Ao, pp32Be, pp32Bo, sumAe, sumAo, sumBe, sumBo,
              ssumAe, ssumAo, ssumBe, ssumBo;
    vec_u8 fsum, sumv, sum;
    vec_s16 ssume, ssumo;

#if HAVE_BIGENDIAN
    permM2 = vec_lvsl(-2, src);
    permM1 = vec_lvsl(-1, src);
    permP0 = vec_lvsl(+0, src);
    permP1 = vec_lvsl(+1, src);
    permP2 = vec_lvsl(+2, src);
    permP3 = vec_lvsl(+3, src);
#endif /* HAVE_BIGENDIAN */

    src -= (2 * srcStride);
    for (i = 0 ; i < 21 ; i ++) {
        vec_u8 srcM2, srcM1, srcP0, srcP1, srcP2, srcP3;

        load_alignment(src, align, permM2, permM1, permP0, permP1, permP2, permP3);

        srcP0A = (vec_s16) VEC_MERGEH(zero_u8v, srcP0);
        srcP0B = (vec_s16) VEC_MERGEL(zero_u8v, srcP0);
        srcP1A = (vec_s16) VEC_MERGEH(zero_u8v, srcP1);
        srcP1B = (vec_s16) VEC_MERGEL(zero_u8v, srcP1);

        srcP2A = (vec_s16) VEC_MERGEH(zero_u8v, srcP2);
        srcP2B = (vec_s16) VEC_MERGEL(zero_u8v, srcP2);
        srcP3A = (vec_s16) VEC_MERGEH(zero_u8v, srcP3);
        srcP3B = (vec_s16) VEC_MERGEL(zero_u8v, srcP3);

        srcM1A = (vec_s16) VEC_MERGEH(zero_u8v, srcM1);
        srcM1B = (vec_s16) VEC_MERGEL(zero_u8v, srcM1);
        srcM2A = (vec_s16) VEC_MERGEH(zero_u8v, srcM2);
        srcM2B = (vec_s16) VEC_MERGEL(zero_u8v, srcM2);

        sum1A = vec_adds(srcP0A, srcP1A);
        sum1B = vec_adds(srcP0B, srcP1B);
        sum2A = vec_adds(srcM1A, srcP2A);
        sum2B = vec_adds(srcM1B, srcP2B);
        sum3A = vec_adds(srcM2A, srcP3A);
        sum3B = vec_adds(srcM2B, srcP3B);

        pp1A = vec_mladd(sum1A, v20ss, sum3A);
        pp1B = vec_mladd(sum1B, v20ss, sum3B);

        pp2A = vec_mladd(sum2A, v5ss, zero_s16v);
        pp2B = vec_mladd(sum2B, v5ss, zero_s16v);

        psumA = vec_sub(pp1A, pp2A);
        psumB = vec_sub(pp1B, pp2B);

        vec_st(psumA, 0, tmp);
        vec_st(psumB, 16, tmp);

        src += srcStride;
        tmp += tmpStride; /* int16_t*, and stride is 16, so it's OK here */
    }

    tmpM2ssA = vec_ld(0, tmpbis);
    tmpM2ssB = vec_ld(16, tmpbis);
    tmpbis += tmpStride;
    tmpM1ssA = vec_ld(0, tmpbis);
    tmpM1ssB = vec_ld(16, tmpbis);
    tmpbis += tmpStride;
    tmpP0ssA = vec_ld(0, tmpbis);
    tmpP0ssB = vec_ld(16, tmpbis);
    tmpbis += tmpStride;
    tmpP1ssA = vec_ld(0, tmpbis);
    tmpP1ssB = vec_ld(16, tmpbis);
    tmpbis += tmpStride;
    tmpP2ssA = vec_ld(0, tmpbis);
    tmpP2ssB = vec_ld(16, tmpbis);
    tmpbis += tmpStride;

    for (i = 0 ; i < 16 ; i++) {
        const vec_s16 tmpP3ssA = vec_ld(0, tmpbis);
        const vec_s16 tmpP3ssB = vec_ld(16, tmpbis);

        const vec_s16 sum1A = vec_adds(tmpP0ssA, tmpP1ssA);
        const vec_s16 sum1B = vec_adds(tmpP0ssB, tmpP1ssB);
        const vec_s16 sum2A = vec_adds(tmpM1ssA, tmpP2ssA);
        const vec_s16 sum2B = vec_adds(tmpM1ssB, tmpP2ssB);
        vec_s16 sum3A = vec_adds(tmpM2ssA, tmpP3ssA);
        vec_s16 sum3B = vec_adds(tmpM2ssB, tmpP3ssB);

        tmpbis += tmpStride;

        tmpM2ssA = tmpM1ssA;
        tmpM2ssB = tmpM1ssB;
        tmpM1ssA = tmpP0ssA;
        tmpM1ssB = tmpP0ssB;
        tmpP0ssA = tmpP1ssA;
        tmpP0ssB = tmpP1ssB;
        tmpP1ssA = tmpP2ssA;
        tmpP1ssB = tmpP2ssB;
        tmpP2ssA = tmpP3ssA;
        tmpP2ssB = tmpP3ssB;

        pp1Ae = vec_mule(sum1A, v20ss);
        pp1Ao = vec_mulo(sum1A, v20ss);
        pp1Be = vec_mule(sum1B, v20ss);
        pp1Bo = vec_mulo(sum1B, v20ss);

        pp2Ae = vec_mule(sum2A, v5ss);
        pp2Ao = vec_mulo(sum2A, v5ss);
        pp2Be = vec_mule(sum2B, v5ss);
        pp2Bo = vec_mulo(sum2B, v5ss);

        pp3Ao = vec_mulo(sum3A, v1ss);
        pp3Bo = vec_mulo(sum3B, v1ss);
#if !HAVE_BIGENDIAN
        sum3A = (vec_s16)vec_perm(sum3A, sum3A,vcswapi2s(0,1,2,3));
        sum3B = (vec_s16)vec_perm(sum3B, sum3B,vcswapi2s(0,1,2,3));
#endif
        pp3Ae = vec_sra((vec_s32)sum3A, v16ui);
        pp3Be = vec_sra((vec_s32)sum3B, v16ui);

        pp1cAe = vec_add(pp1Ae, v512si);
        pp1cAo = vec_add(pp1Ao, v512si);
        pp1cBe = vec_add(pp1Be, v512si);
        pp1cBo = vec_add(pp1Bo, v512si);

        pp32Ae = vec_sub(pp3Ae, pp2Ae);
        pp32Ao = vec_sub(pp3Ao, pp2Ao);
        pp32Be = vec_sub(pp3Be, pp2Be);
        pp32Bo = vec_sub(pp3Bo, pp2Bo);

        sumAe = vec_add(pp1cAe, pp32Ae);
        sumAo = vec_add(pp1cAo, pp32Ao);
        sumBe = vec_add(pp1cBe, pp32Be);
        sumBo = vec_add(pp1cBo, pp32Bo);

        ssumAe = vec_sra(sumAe, v10ui);
        ssumAo = vec_sra(sumAo, v10ui);
        ssumBe = vec_sra(sumBe, v10ui);
        ssumBo = vec_sra(sumBo, v10ui);

        ssume = vec_packs(ssumAe, ssumBe);
        ssumo = vec_packs(ssumAo, ssumBo);

        sumv = vec_packsu(ssume, ssumo);
        sum = vec_perm(sumv, sumv, mperm);

        ASSERT_ALIGNED(dst);

        OP_U8_ALTIVEC(fsum, sum, vec_ld(0, dst));

        vec_st(fsum, 0, dst);

        dst += dstStride;
    }
}
#endif /* PREFIX_h264_qpel16_hv_lowpass_altivec */
