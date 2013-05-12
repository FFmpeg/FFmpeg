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

#include "libavutil/mem.h"

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

#ifdef PREFIX_h264_chroma_mc8_altivec
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
#endif

/* this code assume that stride % 16 == 0 */
#ifdef PREFIX_no_rnd_vc1_chroma_mc8_altivec
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
#endif

#undef noop
#undef add28
#undef CHROMA_MC8_ALTIVEC_CORE
