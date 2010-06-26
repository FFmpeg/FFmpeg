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

//#define DEBUG_ALIGNMENT
#ifdef DEBUG_ALIGNMENT
#define ASSERT_ALIGNED(ptr) assert(((unsigned long)ptr&0x0000000F));
#else
#define ASSERT_ALIGNED(ptr) ;
#endif

/* this code assume that stride % 16 == 0 */

#define CHROMA_MC8_ALTIVEC_CORE(BIAS1, BIAS2) \
        vsrc2ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc2uc);\
        vsrc3ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc3uc);\
\
        psum = vec_mladd(vA, vsrc0ssH, BIAS1);\
        psum = vec_mladd(vB, vsrc1ssH, psum);\
        psum = vec_mladd(vC, vsrc2ssH, psum);\
        psum = vec_mladd(vD, vsrc3ssH, psum);\
        psum = BIAS2(psum);\
        psum = vec_sr(psum, v6us);\
\
        vdst = vec_ld(0, dst);\
        ppsum = (vec_u8)vec_pack(psum, psum);\
        vfdst = vec_perm(vdst, ppsum, fperm);\
\
        OP_U8_ALTIVEC(fsum, vfdst, vdst);\
\
        vec_st(fsum, 0, dst);\
\
        vsrc0ssH = vsrc2ssH;\
        vsrc1ssH = vsrc3ssH;\
\
        dst += stride;\
        src += stride;

#define CHROMA_MC8_ALTIVEC_CORE_SIMPLE \
\
        vsrc0ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc0uc);\
        vsrc1ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc1uc);\
\
        psum = vec_mladd(vA, vsrc0ssH, v32ss);\
        psum = vec_mladd(vE, vsrc1ssH, psum);\
        psum = vec_sr(psum, v6us);\
\
        vdst = vec_ld(0, dst);\
        ppsum = (vec_u8)vec_pack(psum, psum);\
        vfdst = vec_perm(vdst, ppsum, fperm);\
\
        OP_U8_ALTIVEC(fsum, vfdst, vdst);\
\
        vec_st(fsum, 0, dst);\
\
        dst += stride;\
        src += stride;

#define noop(a) a
#define add28(a) vec_add(v28ss, a)

static void PREFIX_h264_chroma_mc8_altivec(uint8_t * dst, uint8_t * src,
                                    int stride, int h, int x, int y) {
    DECLARE_ALIGNED(16, signed int, ABCD)[4] =
                        {((8 - x) * (8 - y)),
                         ((    x) * (8 - y)),
                         ((8 - x) * (    y)),
                         ((    x) * (    y))};
    register int i;
    vec_u8 fperm;
    const vec_s32 vABCD = vec_ld(0, ABCD);
    const vec_s16 vA = vec_splat((vec_s16)vABCD, 1);
    const vec_s16 vB = vec_splat((vec_s16)vABCD, 3);
    const vec_s16 vC = vec_splat((vec_s16)vABCD, 5);
    const vec_s16 vD = vec_splat((vec_s16)vABCD, 7);
    LOAD_ZERO;
    const vec_s16 v32ss = vec_sl(vec_splat_s16(1),vec_splat_u16(5));
    const vec_u16 v6us = vec_splat_u16(6);
    register int loadSecond = (((unsigned long)src) % 16) <= 7 ? 0 : 1;
    register int reallyBadAlign = (((unsigned long)src) % 16) == 15 ? 1 : 0;

    vec_u8 vsrcAuc, av_uninit(vsrcBuc), vsrcperm0, vsrcperm1;
    vec_u8 vsrc0uc, vsrc1uc;
    vec_s16 vsrc0ssH, vsrc1ssH;
    vec_u8 vsrcCuc, vsrc2uc, vsrc3uc;
    vec_s16 vsrc2ssH, vsrc3ssH, psum;
    vec_u8 vdst, ppsum, vfdst, fsum;

    if (((unsigned long)dst) % 16 == 0) {
        fperm = (vec_u8){0x10, 0x11, 0x12, 0x13,
                         0x14, 0x15, 0x16, 0x17,
                         0x08, 0x09, 0x0A, 0x0B,
                         0x0C, 0x0D, 0x0E, 0x0F};
    } else {
        fperm = (vec_u8){0x00, 0x01, 0x02, 0x03,
                         0x04, 0x05, 0x06, 0x07,
                         0x18, 0x19, 0x1A, 0x1B,
                         0x1C, 0x1D, 0x1E, 0x1F};
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

    vsrc0ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc0uc);
    vsrc1ssH = (vec_s16)vec_mergeh(zero_u8v,(vec_u8)vsrc1uc);

    if (ABCD[3]) {
        if (!loadSecond) {// -> !reallyBadAlign
            for (i = 0 ; i < h ; i++) {
                vsrcCuc = vec_ld(stride + 0, src);
                vsrc2uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm0);
                vsrc3uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm1);

                CHROMA_MC8_ALTIVEC_CORE(v32ss, noop)
            }
        } else {
            vec_u8 vsrcDuc;
            for (i = 0 ; i < h ; i++) {
                vsrcCuc = vec_ld(stride + 0, src);
                vsrcDuc = vec_ld(stride + 16, src);
                vsrc2uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm0);
                if (reallyBadAlign)
                    vsrc3uc = vsrcDuc;
                else
                    vsrc3uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm1);

                CHROMA_MC8_ALTIVEC_CORE(v32ss, noop)
            }
        }
    } else {
        const vec_s16 vE = vec_add(vB, vC);
        if (ABCD[2]) { // x == 0 B == 0
            if (!loadSecond) {// -> !reallyBadAlign
                for (i = 0 ; i < h ; i++) {
                    vsrcCuc = vec_ld(stride + 0, src);
                    vsrc1uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm0);
                    CHROMA_MC8_ALTIVEC_CORE_SIMPLE

                    vsrc0uc = vsrc1uc;
                }
            } else {
                vec_u8 vsrcDuc;
                for (i = 0 ; i < h ; i++) {
                    vsrcCuc = vec_ld(stride + 0, src);
                    vsrcDuc = vec_ld(stride + 15, src);
                    vsrc1uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm0);
                    CHROMA_MC8_ALTIVEC_CORE_SIMPLE

                    vsrc0uc = vsrc1uc;
                }
            }
        } else { // y == 0 C == 0
            if (!loadSecond) {// -> !reallyBadAlign
                for (i = 0 ; i < h ; i++) {
                    vsrcCuc = vec_ld(0, src);
                    vsrc0uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm0);
                    vsrc1uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm1);

                    CHROMA_MC8_ALTIVEC_CORE_SIMPLE
                }
            } else {
                vec_u8 vsrcDuc;
                for (i = 0 ; i < h ; i++) {
                    vsrcCuc = vec_ld(0, src);
                    vsrcDuc = vec_ld(15, src);
                    vsrc0uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm0);
                    if (reallyBadAlign)
                        vsrc1uc = vsrcDuc;
                    else
                        vsrc1uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm1);

                    CHROMA_MC8_ALTIVEC_CORE_SIMPLE
                }
            }
        }
    }
}

/* this code assume that stride % 16 == 0 */
static void PREFIX_no_rnd_vc1_chroma_mc8_altivec(uint8_t * dst, uint8_t * src, int stride, int h, int x, int y) {
   DECLARE_ALIGNED(16, signed int, ABCD)[4] =
                        {((8 - x) * (8 - y)),
                         ((    x) * (8 - y)),
                         ((8 - x) * (    y)),
                         ((    x) * (    y))};
    register int i;
    vec_u8 fperm;
    const vec_s32 vABCD = vec_ld(0, ABCD);
    const vec_s16 vA = vec_splat((vec_s16)vABCD, 1);
    const vec_s16 vB = vec_splat((vec_s16)vABCD, 3);
    const vec_s16 vC = vec_splat((vec_s16)vABCD, 5);
    const vec_s16 vD = vec_splat((vec_s16)vABCD, 7);
    LOAD_ZERO;
    const vec_s16 v28ss = vec_sub(vec_sl(vec_splat_s16(1),vec_splat_u16(5)),vec_splat_s16(4));
    const vec_u16 v6us  = vec_splat_u16(6);
    register int loadSecond     = (((unsigned long)src) % 16) <= 7 ? 0 : 1;
    register int reallyBadAlign = (((unsigned long)src) % 16) == 15 ? 1 : 0;

    vec_u8 vsrcAuc, av_uninit(vsrcBuc), vsrcperm0, vsrcperm1;
    vec_u8 vsrc0uc, vsrc1uc;
    vec_s16 vsrc0ssH, vsrc1ssH;
    vec_u8 vsrcCuc, vsrc2uc, vsrc3uc;
    vec_s16 vsrc2ssH, vsrc3ssH, psum;
    vec_u8 vdst, ppsum, vfdst, fsum;

    if (((unsigned long)dst) % 16 == 0) {
        fperm = (vec_u8){0x10, 0x11, 0x12, 0x13,
                         0x14, 0x15, 0x16, 0x17,
                         0x08, 0x09, 0x0A, 0x0B,
                         0x0C, 0x0D, 0x0E, 0x0F};
    } else {
        fperm = (vec_u8){0x00, 0x01, 0x02, 0x03,
                         0x04, 0x05, 0x06, 0x07,
                         0x18, 0x19, 0x1A, 0x1B,
                         0x1C, 0x1D, 0x1E, 0x1F};
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

    vsrc0ssH = (vec_s16)vec_mergeh(zero_u8v, (vec_u8)vsrc0uc);
    vsrc1ssH = (vec_s16)vec_mergeh(zero_u8v, (vec_u8)vsrc1uc);

    if (!loadSecond) {// -> !reallyBadAlign
        for (i = 0 ; i < h ; i++) {


            vsrcCuc = vec_ld(stride + 0, src);

            vsrc2uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm0);
            vsrc3uc = vec_perm(vsrcCuc, vsrcCuc, vsrcperm1);

            CHROMA_MC8_ALTIVEC_CORE(vec_splat_s16(0), add28)
        }
    } else {
        vec_u8 vsrcDuc;
        for (i = 0 ; i < h ; i++) {
            vsrcCuc = vec_ld(stride + 0, src);
            vsrcDuc = vec_ld(stride + 16, src);

            vsrc2uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm0);
            if (reallyBadAlign)
                vsrc3uc = vsrcDuc;
            else
                vsrc3uc = vec_perm(vsrcCuc, vsrcDuc, vsrcperm1);

            CHROMA_MC8_ALTIVEC_CORE(vec_splat_s16(0), add28)
        }
    }
}

#undef noop
#undef add28
#undef CHROMA_MC8_ALTIVEC_CORE

/* this code assume stride % 16 == 0 */
static void PREFIX_h264_qpel16_h_lowpass_altivec(uint8_t * dst, uint8_t * src, int dstStride, int srcStride) {
    register int i;

    LOAD_ZERO;
    const vec_u8 permM2 = vec_lvsl(-2, src);
    const vec_u8 permM1 = vec_lvsl(-1, src);
    const vec_u8 permP0 = vec_lvsl(+0, src);
    const vec_u8 permP1 = vec_lvsl(+1, src);
    const vec_u8 permP2 = vec_lvsl(+2, src);
    const vec_u8 permP3 = vec_lvsl(+3, src);
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

    vec_u8 sum, vdst, fsum;

    for (i = 0 ; i < 16 ; i ++) {
        vec_u8 srcR1 = vec_ld(-2, src);
        vec_u8 srcR2 = vec_ld(14, src);

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
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = vec_perm(srcR1, srcR2, permP0);
            srcP1 = vec_perm(srcR1, srcR2, permP1);
            srcP2 = srcR2;
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 13: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = vec_perm(srcR1, srcR2, permP0);
            srcP1 = srcR2;
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 14: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = srcR2;
            srcP1 = vec_perm(srcR2, srcR3, permP1);
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 15: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = srcR2;
            srcP0 = vec_perm(srcR2, srcR3, permP0);
            srcP1 = vec_perm(srcR2, srcR3, permP1);
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        }

        srcP0A = (vec_s16) vec_mergeh(zero_u8v, srcP0);
        srcP0B = (vec_s16) vec_mergel(zero_u8v, srcP0);
        srcP1A = (vec_s16) vec_mergeh(zero_u8v, srcP1);
        srcP1B = (vec_s16) vec_mergel(zero_u8v, srcP1);

        srcP2A = (vec_s16) vec_mergeh(zero_u8v, srcP2);
        srcP2B = (vec_s16) vec_mergel(zero_u8v, srcP2);
        srcP3A = (vec_s16) vec_mergeh(zero_u8v, srcP3);
        srcP3B = (vec_s16) vec_mergel(zero_u8v, srcP3);

        srcM1A = (vec_s16) vec_mergeh(zero_u8v, srcM1);
        srcM1B = (vec_s16) vec_mergel(zero_u8v, srcM1);
        srcM2A = (vec_s16) vec_mergeh(zero_u8v, srcM2);
        srcM2B = (vec_s16) vec_mergel(zero_u8v, srcM2);

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
        vdst = vec_ld(0, dst);

        OP_U8_ALTIVEC(fsum, sum, vdst);

        vec_st(fsum, 0, dst);

        src += srcStride;
        dst += dstStride;
    }
}

/* this code assume stride % 16 == 0 */
static void PREFIX_h264_qpel16_v_lowpass_altivec(uint8_t * dst, uint8_t * src, int dstStride, int srcStride) {
    register int i;

    LOAD_ZERO;
    const vec_u8 perm = vec_lvsl(0, src);
    const vec_s16 v20ss = vec_sl(vec_splat_s16(5),vec_splat_u16(2));
    const vec_u16 v5us = vec_splat_u16(5);
    const vec_s16 v5ss = vec_splat_s16(5);
    const vec_s16 v16ss = vec_sl(vec_splat_s16(1),vec_splat_u16(4));

    uint8_t *srcbis = src - (srcStride * 2);

    const vec_u8 srcM2a = vec_ld(0, srcbis);
    const vec_u8 srcM2b = vec_ld(16, srcbis);
    const vec_u8 srcM2 = vec_perm(srcM2a, srcM2b, perm);
    //srcbis += srcStride;
    const vec_u8 srcM1a = vec_ld(0, srcbis += srcStride);
    const vec_u8 srcM1b = vec_ld(16, srcbis);
    const vec_u8 srcM1 = vec_perm(srcM1a, srcM1b, perm);
    //srcbis += srcStride;
    const vec_u8 srcP0a = vec_ld(0, srcbis += srcStride);
    const vec_u8 srcP0b = vec_ld(16, srcbis);
    const vec_u8 srcP0 = vec_perm(srcP0a, srcP0b, perm);
    //srcbis += srcStride;
    const vec_u8 srcP1a = vec_ld(0, srcbis += srcStride);
    const vec_u8 srcP1b = vec_ld(16, srcbis);
    const vec_u8 srcP1 = vec_perm(srcP1a, srcP1b, perm);
    //srcbis += srcStride;
    const vec_u8 srcP2a = vec_ld(0, srcbis += srcStride);
    const vec_u8 srcP2b = vec_ld(16, srcbis);
    const vec_u8 srcP2 = vec_perm(srcP2a, srcP2b, perm);
    //srcbis += srcStride;

    vec_s16 srcM2ssA = (vec_s16) vec_mergeh(zero_u8v, srcM2);
    vec_s16 srcM2ssB = (vec_s16) vec_mergel(zero_u8v, srcM2);
    vec_s16 srcM1ssA = (vec_s16) vec_mergeh(zero_u8v, srcM1);
    vec_s16 srcM1ssB = (vec_s16) vec_mergel(zero_u8v, srcM1);
    vec_s16 srcP0ssA = (vec_s16) vec_mergeh(zero_u8v, srcP0);
    vec_s16 srcP0ssB = (vec_s16) vec_mergel(zero_u8v, srcP0);
    vec_s16 srcP1ssA = (vec_s16) vec_mergeh(zero_u8v, srcP1);
    vec_s16 srcP1ssB = (vec_s16) vec_mergel(zero_u8v, srcP1);
    vec_s16 srcP2ssA = (vec_s16) vec_mergeh(zero_u8v, srcP2);
    vec_s16 srcP2ssB = (vec_s16) vec_mergel(zero_u8v, srcP2);

    vec_s16 pp1A, pp1B, pp2A, pp2B, pp3A, pp3B,
              psumA, psumB, sumA, sumB,
              srcP3ssA, srcP3ssB,
              sum1A, sum1B, sum2A, sum2B, sum3A, sum3B;

    vec_u8 sum, vdst, fsum, srcP3a, srcP3b, srcP3;

    for (i = 0 ; i < 16 ; i++) {
        srcP3a = vec_ld(0, srcbis += srcStride);
        srcP3b = vec_ld(16, srcbis);
        srcP3 = vec_perm(srcP3a, srcP3b, perm);
        srcP3ssA = (vec_s16) vec_mergeh(zero_u8v, srcP3);
        srcP3ssB = (vec_s16) vec_mergel(zero_u8v, srcP3);
        //srcbis += srcStride;

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
        vdst = vec_ld(0, dst);

        OP_U8_ALTIVEC(fsum, sum, vdst);

        vec_st(fsum, 0, dst);

        dst += dstStride;
    }
}

/* this code assume stride % 16 == 0 *and* tmp is properly aligned */
static void PREFIX_h264_qpel16_hv_lowpass_altivec(uint8_t * dst, int16_t * tmp, uint8_t * src, int dstStride, int tmpStride, int srcStride) {
    register int i;
    LOAD_ZERO;
    const vec_u8 permM2 = vec_lvsl(-2, src);
    const vec_u8 permM1 = vec_lvsl(-1, src);
    const vec_u8 permP0 = vec_lvsl(+0, src);
    const vec_u8 permP1 = vec_lvsl(+1, src);
    const vec_u8 permP2 = vec_lvsl(+2, src);
    const vec_u8 permP3 = vec_lvsl(+3, src);
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
    vec_u8 fsum, sumv, sum, vdst;
    vec_s16 ssume, ssumo;

    src -= (2 * srcStride);
    for (i = 0 ; i < 21 ; i ++) {
        vec_u8 srcM2, srcM1, srcP0, srcP1, srcP2, srcP3;
        vec_u8 srcR1 = vec_ld(-2, src);
        vec_u8 srcR2 = vec_ld(14, src);

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
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = vec_perm(srcR1, srcR2, permP0);
            srcP1 = vec_perm(srcR1, srcR2, permP1);
            srcP2 = srcR2;
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 13: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = vec_perm(srcR1, srcR2, permP0);
            srcP1 = srcR2;
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 14: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = vec_perm(srcR1, srcR2, permM1);
            srcP0 = srcR2;
            srcP1 = vec_perm(srcR2, srcR3, permP1);
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        case 15: {
            vec_u8 srcR3 = vec_ld(30, src);
            srcM2 = vec_perm(srcR1, srcR2, permM2);
            srcM1 = srcR2;
            srcP0 = vec_perm(srcR2, srcR3, permP0);
            srcP1 = vec_perm(srcR2, srcR3, permP1);
            srcP2 = vec_perm(srcR2, srcR3, permP2);
            srcP3 = vec_perm(srcR2, srcR3, permP3);
        } break;
        }

        srcP0A = (vec_s16) vec_mergeh(zero_u8v, srcP0);
        srcP0B = (vec_s16) vec_mergel(zero_u8v, srcP0);
        srcP1A = (vec_s16) vec_mergeh(zero_u8v, srcP1);
        srcP1B = (vec_s16) vec_mergel(zero_u8v, srcP1);

        srcP2A = (vec_s16) vec_mergeh(zero_u8v, srcP2);
        srcP2B = (vec_s16) vec_mergel(zero_u8v, srcP2);
        srcP3A = (vec_s16) vec_mergeh(zero_u8v, srcP3);
        srcP3B = (vec_s16) vec_mergel(zero_u8v, srcP3);

        srcM1A = (vec_s16) vec_mergeh(zero_u8v, srcM1);
        srcM1B = (vec_s16) vec_mergel(zero_u8v, srcM1);
        srcM2A = (vec_s16) vec_mergeh(zero_u8v, srcM2);
        srcM2B = (vec_s16) vec_mergel(zero_u8v, srcM2);

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
        const vec_s16 sum3A = vec_adds(tmpM2ssA, tmpP3ssA);
        const vec_s16 sum3B = vec_adds(tmpM2ssB, tmpP3ssB);

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

        pp3Ae = vec_sra((vec_s32)sum3A, v16ui);
        pp3Ao = vec_mulo(sum3A, v1ss);
        pp3Be = vec_sra((vec_s32)sum3B, v16ui);
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

        ASSERT_ALIGNED(dst);
        vdst = vec_ld(0, dst);

        OP_U8_ALTIVEC(fsum, sum, vdst);

        vec_st(fsum, 0, dst);

        dst += dstStride;
    }
}
