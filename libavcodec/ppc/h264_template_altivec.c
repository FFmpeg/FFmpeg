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

/* this code assume that stride % 16 == 0 */
void PREFIX_h264_chroma_mc8_altivec(uint8_t * dst, uint8_t * src, int stride, int h, int x, int y) {
  POWERPC_PERF_DECLARE(PREFIX_h264_chroma_mc8_num, 1);
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
    const vector signed short v32ss = vec_sl(vec_splat_s16(1),vec_splat_u16(5));
    const vector unsigned short v6us = vec_splat_u16(6);
    register int loadSecond = (((unsigned long)src) % 16) <= 7 ? 0 : 1;
    register int reallyBadAlign = (((unsigned long)src) % 16) == 15 ? 1 : 0;

    vector unsigned char vsrcAuc, vsrcBuc, vsrcperm0, vsrcperm1;
    vector unsigned char vsrc0uc, vsrc1uc;
    vector signed short vsrc0ssH, vsrc1ssH;
    vector unsigned char vsrcCuc, vsrc2uc, vsrc3uc;
    vector signed short vsrc2ssH, vsrc3ssH, psum;
    vector unsigned char vdst, ppsum, vfdst, fsum;

  POWERPC_PERF_START_COUNT(PREFIX_h264_chroma_mc8_num, 1);

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
        psum = vec_add(v32ss, psum);
        psum = vec_sra(psum, v6us);

        vdst = vec_ld(0, dst);
        ppsum = (vector unsigned char)vec_packsu(psum, psum);
        vfdst = vec_perm(vdst, ppsum, fperm);

        OP_U8_ALTIVEC(fsum, vfdst, vdst);

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
        psum = vec_add(v32ss, psum);
        psum = vec_sr(psum, v6us);

        vdst = vec_ld(0, dst);
        ppsum = (vector unsigned char)vec_pack(psum, psum);
        vfdst = vec_perm(vdst, ppsum, fperm);

        OP_U8_ALTIVEC(fsum, vfdst, vdst);

        vec_st(fsum, 0, dst);

        vsrc0ssH = vsrc2ssH;
        vsrc1ssH = vsrc3ssH;

        dst += stride;
        src += stride;
      }
    }
    POWERPC_PERF_STOP_COUNT(PREFIX_h264_chroma_mc8_num, 1);
}

/* this code assume stride % 16 == 0 */
static void PREFIX_h264_qpel16_h_lowpass_altivec(uint8_t * dst, uint8_t * src, int dstStride, int srcStride) {
  POWERPC_PERF_DECLARE(PREFIX_h264_qpel16_h_lowpass_num, 1);
  register int i;

  const vector signed int vzero = vec_splat_s32(0);
  const vector unsigned char permM2 = vec_lvsl(-2, src);
  const vector unsigned char permM1 = vec_lvsl(-1, src);
  const vector unsigned char permP0 = vec_lvsl(+0, src);
  const vector unsigned char permP1 = vec_lvsl(+1, src);
  const vector unsigned char permP2 = vec_lvsl(+2, src);
  const vector unsigned char permP3 = vec_lvsl(+3, src);
  const vector signed short v5ss = vec_splat_s16(5);
  const vector unsigned short v5us = vec_splat_u16(5);
  const vector signed short v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
  const vector signed short v16ss = vec_sl(vec_splat_s16(1),vec_splat_u16(4));
  const vector unsigned char dstperm = vec_lvsr(0, dst);
  const vector unsigned char neg1 =
                                (const vector unsigned char) vec_splat_s8(-1);

  const vector unsigned char dstmask =
                                vec_perm((const vector unsigned char)vzero,
                                                               neg1, dstperm);

  vector unsigned char srcM2, srcM1, srcP0, srcP1, srcP2, srcP3;

  register int align = ((((unsigned long)src) - 2) % 16);

  vector signed short srcP0A, srcP0B, srcP1A, srcP1B,
                      srcP2A, srcP2B, srcP3A, srcP3B,
                      srcM1A, srcM1B, srcM2A, srcM2B,
                      sum1A, sum1B, sum2A, sum2B, sum3A, sum3B,
                      pp1A, pp1B, pp2A, pp2B, pp3A, pp3B,
                      psumA, psumB, sumA, sumB;

  vector unsigned char sum, dst1, dst2, vdst, fsum,
                       rsum, fdst1, fdst2;

  POWERPC_PERF_START_COUNT(PREFIX_h264_qpel16_h_lowpass_num, 1);

  for (i = 0 ; i < 16 ; i ++) {
    vector unsigned char srcR1 = vec_ld(-2, src);
    vector unsigned char srcR2 = vec_ld(14, src);

    switch (align) {
    default: {
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = vec_perm(srcR1, srcR2, permP2);
      srcP3 = vec_perm(srcR1, srcR2, permP3);
    } break;
    case 11: {
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = vec_perm(srcR1, srcR2, permP2);
      srcP3 = srcR2;
    } break;
    case 12: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = srcR2;
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 13: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = srcR2;
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 14: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = srcR2;
      srcP1 = vec_perm(srcR2, srcR3, permP1);
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 15: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = srcR2;
      srcP0 = vec_perm(srcR2, srcR3, permP0);
      srcP1 = vec_perm(srcR2, srcR3, permP1);
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    }

    srcP0A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcP0);
    srcP0B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcP0);
    srcP1A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcP1);
    srcP1B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcP1);

    srcP2A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcP2);
    srcP2B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcP2);
    srcP3A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcP3);
    srcP3B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcP3);

    srcM1A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcM1);
    srcM1B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcM1);
    srcM2A = (vector signed short)
                vec_mergeh((vector unsigned char)vzero, srcM2);
    srcM2B = (vector signed short)
                vec_mergel((vector unsigned char)vzero, srcM2);

    sum1A = vec_adds(srcP0A, srcP1A);
    sum1B = vec_adds(srcP0B, srcP1B);
    sum2A = vec_adds(srcM1A, srcP2A);
    sum2B = vec_adds(srcM1B, srcP2B);
    sum3A = vec_adds(srcM2A, srcP3A);
    sum3B = vec_adds(srcM2B, srcP3B);

    pp1A = vec_mladd(sum1A, v20ss, v16ss);
    pp1B = vec_mladd(sum1B, v20ss, v16ss);

    pp2A = vec_mladd(sum2A, v5ss, (vector signed short)vzero);
    pp2B = vec_mladd(sum2B, v5ss, (vector signed short)vzero);

    pp3A = vec_add(sum3A, pp1A);
    pp3B = vec_add(sum3B, pp1B);

    psumA = vec_sub(pp3A, pp2A);
    psumB = vec_sub(pp3B, pp2B);

    sumA = vec_sra(psumA, v5us);
    sumB = vec_sra(psumB, v5us);

    sum = vec_packsu(sumA, sumB);

    dst1 = vec_ld(0, dst);
    dst2 = vec_ld(16, dst);
    vdst = vec_perm(dst1, dst2, vec_lvsl(0, dst));

    OP_U8_ALTIVEC(fsum, sum, vdst);

    rsum = vec_perm(fsum, fsum, dstperm);
    fdst1 = vec_sel(dst1, rsum, dstmask);
    fdst2 = vec_sel(rsum, dst2, dstmask);

    vec_st(fdst1, 0, dst);
    vec_st(fdst2, 16, dst);

    src += srcStride;
    dst += dstStride;
  }
POWERPC_PERF_STOP_COUNT(PREFIX_h264_qpel16_h_lowpass_num, 1);
}

/* this code assume stride % 16 == 0 */
static void PREFIX_h264_qpel16_v_lowpass_altivec(uint8_t * dst, uint8_t * src, int dstStride, int srcStride) {
  POWERPC_PERF_DECLARE(PREFIX_h264_qpel16_v_lowpass_num, 1);

  register int i;

  const vector signed int vzero = vec_splat_s32(0);
  const vector unsigned char perm = vec_lvsl(0, src);
  const vector signed short v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
  const vector unsigned short v5us = vec_splat_u16(5);
  const vector signed short v5ss = vec_splat_s16(5);
  const vector signed short v16ss = vec_sl(vec_splat_s16(1),vec_splat_u16(4));
  const vector unsigned char dstperm = vec_lvsr(0, dst);
  const vector unsigned char neg1 = (const vector unsigned char)vec_splat_s8(-1);
  const vector unsigned char dstmask = vec_perm((const vector unsigned char)vzero, neg1, dstperm);

  uint8_t *srcbis = src - (srcStride * 2);

  const vector unsigned char srcM2a = vec_ld(0, srcbis);
  const vector unsigned char srcM2b = vec_ld(16, srcbis);
  const vector unsigned char srcM2 = vec_perm(srcM2a, srcM2b, perm);
//  srcbis += srcStride;
  const vector unsigned char srcM1a = vec_ld(0, srcbis += srcStride);
  const vector unsigned char srcM1b = vec_ld(16, srcbis);
  const vector unsigned char srcM1 = vec_perm(srcM1a, srcM1b, perm);
//  srcbis += srcStride;
  const vector unsigned char srcP0a = vec_ld(0, srcbis += srcStride);
  const vector unsigned char srcP0b = vec_ld(16, srcbis);
  const vector unsigned char srcP0 = vec_perm(srcP0a, srcP0b, perm);
//  srcbis += srcStride;
  const vector unsigned char srcP1a = vec_ld(0, srcbis += srcStride);
  const vector unsigned char srcP1b = vec_ld(16, srcbis);
  const vector unsigned char srcP1 = vec_perm(srcP1a, srcP1b, perm);
//  srcbis += srcStride;
  const vector unsigned char srcP2a = vec_ld(0, srcbis += srcStride);
  const vector unsigned char srcP2b = vec_ld(16, srcbis);
  const vector unsigned char srcP2 = vec_perm(srcP2a, srcP2b, perm);
//  srcbis += srcStride;

  vector signed short srcM2ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcM2);
  vector signed short srcM2ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcM2);
  vector signed short srcM1ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcM1);
  vector signed short srcM1ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcM1);
  vector signed short srcP0ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcP0);
  vector signed short srcP0ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcP0);
  vector signed short srcP1ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcP1);
  vector signed short srcP1ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcP1);
  vector signed short srcP2ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcP2);
  vector signed short srcP2ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcP2);

  vector signed short pp1A, pp1B, pp2A, pp2B, pp3A, pp3B,
                      psumA, psumB, sumA, sumB,
                      srcP3ssA, srcP3ssB,
                      sum1A, sum1B, sum2A, sum2B, sum3A, sum3B;

  vector unsigned char sum, dst1, dst2, vdst, fsum, rsum, fdst1, fdst2,
                       srcP3a, srcP3b, srcP3;

  POWERPC_PERF_START_COUNT(PREFIX_h264_qpel16_v_lowpass_num, 1);

  for (i = 0 ; i < 16 ; i++) {
    srcP3a = vec_ld(0, srcbis += srcStride);
    srcP3b = vec_ld(16, srcbis);
    srcP3 = vec_perm(srcP3a, srcP3b, perm);
    srcP3ssA = (vector signed short)
                                vec_mergeh((vector unsigned char)vzero, srcP3);
    srcP3ssB = (vector signed short)
                                vec_mergel((vector unsigned char)vzero, srcP3);
//    srcbis += srcStride;

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

    pp2A = vec_mladd(sum2A, v5ss, (vector signed short)vzero);
    pp2B = vec_mladd(sum2B, v5ss, (vector signed short)vzero);

    pp3A = vec_add(sum3A, pp1A);
    pp3B = vec_add(sum3B, pp1B);

    psumA = vec_sub(pp3A, pp2A);
    psumB = vec_sub(pp3B, pp2B);

    sumA = vec_sra(psumA, v5us);
    sumB = vec_sra(psumB, v5us);

    sum = vec_packsu(sumA, sumB);

    dst1 = vec_ld(0, dst);
    dst2 = vec_ld(16, dst);
    vdst = vec_perm(dst1, dst2, vec_lvsl(0, dst));

    OP_U8_ALTIVEC(fsum, sum, vdst);

    rsum = vec_perm(fsum, fsum, dstperm);
    fdst1 = vec_sel(dst1, rsum, dstmask);
    fdst2 = vec_sel(rsum, dst2, dstmask);

    vec_st(fdst1, 0, dst);
    vec_st(fdst2, 16, dst);

    dst += dstStride;
  }
  POWERPC_PERF_STOP_COUNT(PREFIX_h264_qpel16_v_lowpass_num, 1);
}

/* this code assume stride % 16 == 0 *and* tmp is properly aligned */
static void PREFIX_h264_qpel16_hv_lowpass_altivec(uint8_t * dst, int16_t * tmp, uint8_t * src, int dstStride, int tmpStride, int srcStride) {
  POWERPC_PERF_DECLARE(PREFIX_h264_qpel16_hv_lowpass_num, 1);
  register int i;
  const vector signed int vzero = vec_splat_s32(0);
  const vector unsigned char permM2 = vec_lvsl(-2, src);
  const vector unsigned char permM1 = vec_lvsl(-1, src);
  const vector unsigned char permP0 = vec_lvsl(+0, src);
  const vector unsigned char permP1 = vec_lvsl(+1, src);
  const vector unsigned char permP2 = vec_lvsl(+2, src);
  const vector unsigned char permP3 = vec_lvsl(+3, src);
  const vector signed short v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
  const vector unsigned int v10ui = vec_splat_u32(10);
  const vector signed short v5ss = vec_splat_s16(5);
  const vector signed short v1ss = vec_splat_s16(1);
  const vector signed int v512si = vec_sl(vec_splat_s32(1),vec_splat_u32(9));
  const vector unsigned int v16ui = vec_sl(vec_splat_u32(1),vec_splat_u32(4));

  register int align = ((((unsigned long)src) - 2) % 16);

  const vector unsigned char neg1 = (const vector unsigned char)
                                                        vec_splat_s8(-1);

  vector signed short srcP0A, srcP0B, srcP1A, srcP1B,
                      srcP2A, srcP2B, srcP3A, srcP3B,
                      srcM1A, srcM1B, srcM2A, srcM2B,
                      sum1A, sum1B, sum2A, sum2B, sum3A, sum3B,
                      pp1A, pp1B, pp2A, pp2B, psumA, psumB;

  const vector unsigned char dstperm = vec_lvsr(0, dst);

  const vector unsigned char dstmask = vec_perm((const vector unsigned char)vzero, neg1, dstperm);

  const vector unsigned char mperm = (const vector unsigned char)
    AVV(0x00, 0x08, 0x01, 0x09, 0x02, 0x0A, 0x03, 0x0B,
        0x04, 0x0C, 0x05, 0x0D, 0x06, 0x0E, 0x07, 0x0F);
  int16_t *tmpbis = tmp;

  vector signed short tmpM1ssA, tmpM1ssB, tmpM2ssA, tmpM2ssB,
                      tmpP0ssA, tmpP0ssB, tmpP1ssA, tmpP1ssB,
                      tmpP2ssA, tmpP2ssB;

  vector signed int pp1Ae, pp1Ao, pp1Be, pp1Bo, pp2Ae, pp2Ao, pp2Be, pp2Bo,
                    pp3Ae, pp3Ao, pp3Be, pp3Bo, pp1cAe, pp1cAo, pp1cBe, pp1cBo,
                    pp32Ae, pp32Ao, pp32Be, pp32Bo, sumAe, sumAo, sumBe, sumBo,
                    ssumAe, ssumAo, ssumBe, ssumBo;
  vector unsigned char fsum, sumv, sum, dst1, dst2, vdst,
                       rsum, fdst1, fdst2;
  vector signed short ssume, ssumo;

  POWERPC_PERF_START_COUNT(PREFIX_h264_qpel16_hv_lowpass_num, 1);
  src -= (2 * srcStride);
  for (i = 0 ; i < 21 ; i ++) {
    vector unsigned char srcM2, srcM1, srcP0, srcP1, srcP2, srcP3;
    vector unsigned char srcR1 = vec_ld(-2, src);
    vector unsigned char srcR2 = vec_ld(14, src);

    switch (align) {
    default: {
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = vec_perm(srcR1, srcR2, permP2);
      srcP3 = vec_perm(srcR1, srcR2, permP3);
    } break;
    case 11: {
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = vec_perm(srcR1, srcR2, permP2);
      srcP3 = srcR2;
    } break;
    case 12: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = vec_perm(srcR1, srcR2, permP1);
      srcP2 = srcR2;
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 13: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = vec_perm(srcR1, srcR2, permP0);
      srcP1 = srcR2;
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 14: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = vec_perm(srcR1, srcR2, permM1);
      srcP0 = srcR2;
      srcP1 = vec_perm(srcR2, srcR3, permP1);
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    case 15: {
      vector unsigned char srcR3 = vec_ld(30, src);
      srcM2 = vec_perm(srcR1, srcR2, permM2);
      srcM1 = srcR2;
      srcP0 = vec_perm(srcR2, srcR3, permP0);
      srcP1 = vec_perm(srcR2, srcR3, permP1);
      srcP2 = vec_perm(srcR2, srcR3, permP2);
      srcP3 = vec_perm(srcR2, srcR3, permP3);
    } break;
    }

    srcP0A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcP0);
    srcP0B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcP0);
    srcP1A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcP1);
    srcP1B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcP1);

    srcP2A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcP2);
    srcP2B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcP2);
    srcP3A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcP3);
    srcP3B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcP3);

    srcM1A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcM1);
    srcM1B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcM1);
    srcM2A = (vector signed short)
                            vec_mergeh((vector unsigned char)vzero, srcM2);
    srcM2B = (vector signed short)
                            vec_mergel((vector unsigned char)vzero, srcM2);

    sum1A = vec_adds(srcP0A, srcP1A);
    sum1B = vec_adds(srcP0B, srcP1B);
    sum2A = vec_adds(srcM1A, srcP2A);
    sum2B = vec_adds(srcM1B, srcP2B);
    sum3A = vec_adds(srcM2A, srcP3A);
    sum3B = vec_adds(srcM2B, srcP3B);

    pp1A = vec_mladd(sum1A, v20ss, sum3A);
    pp1B = vec_mladd(sum1B, v20ss, sum3B);

    pp2A = vec_mladd(sum2A, v5ss, (vector signed short)vzero);
    pp2B = vec_mladd(sum2B, v5ss, (vector signed short)vzero);

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
    const vector signed short tmpP3ssA = vec_ld(0, tmpbis);
    const vector signed short tmpP3ssB = vec_ld(16, tmpbis);

    const vector signed short sum1A = vec_adds(tmpP0ssA, tmpP1ssA);
    const vector signed short sum1B = vec_adds(tmpP0ssB, tmpP1ssB);
    const vector signed short sum2A = vec_adds(tmpM1ssA, tmpP2ssA);
    const vector signed short sum2B = vec_adds(tmpM1ssB, tmpP2ssB);
    const vector signed short sum3A = vec_adds(tmpM2ssA, tmpP3ssA);
    const vector signed short sum3B = vec_adds(tmpM2ssB, tmpP3ssB);

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

    pp3Ae = vec_sra((vector signed int)sum3A, v16ui);
    pp3Ao = vec_mulo(sum3A, v1ss);
    pp3Be = vec_sra((vector signed int)sum3B, v16ui);
    pp3Bo = vec_mulo(sum3B, v1ss);

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

    dst1 = vec_ld(0, dst);
    dst2 = vec_ld(16, dst);
    vdst = vec_perm(dst1, dst2, vec_lvsl(0, dst));

    OP_U8_ALTIVEC(fsum, sum, vdst);

    rsum = vec_perm(fsum, fsum, dstperm);
    fdst1 = vec_sel(dst1, rsum, dstmask);
    fdst2 = vec_sel(rsum, dst2, dstmask);

    vec_st(fdst1, 0, dst);
    vec_st(fdst2, 16, dst);

    dst += dstStride;
  }
  POWERPC_PERF_STOP_COUNT(PREFIX_h264_qpel16_hv_lowpass_num, 1);
}
