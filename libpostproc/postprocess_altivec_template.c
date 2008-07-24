/*
 * AltiVec optimizations (C) 2004 Romain Dolbeau <romain@dolbeau.org>
 *
 * based on code by Copyright (C) 2001-2003 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avutil.h"

#define ALTIVEC_TRANSPOSE_8x8_SHORT(src_a,src_b,src_c,src_d,src_e,src_f,src_g,src_h) \
    do {                                                          \
        __typeof__(src_a) tempA1, tempB1, tempC1, tempD1;         \
        __typeof__(src_a) tempE1, tempF1, tempG1, tempH1;         \
        __typeof__(src_a) tempA2, tempB2, tempC2, tempD2;         \
        __typeof__(src_a) tempE2, tempF2, tempG2, tempH2;         \
        tempA1 = vec_mergeh (src_a, src_e);                       \
        tempB1 = vec_mergel (src_a, src_e);                       \
        tempC1 = vec_mergeh (src_b, src_f);                       \
        tempD1 = vec_mergel (src_b, src_f);                       \
        tempE1 = vec_mergeh (src_c, src_g);                       \
        tempF1 = vec_mergel (src_c, src_g);                       \
        tempG1 = vec_mergeh (src_d, src_h);                       \
        tempH1 = vec_mergel (src_d, src_h);                       \
        tempA2 = vec_mergeh (tempA1, tempE1);                     \
        tempB2 = vec_mergel (tempA1, tempE1);                     \
        tempC2 = vec_mergeh (tempB1, tempF1);                     \
        tempD2 = vec_mergel (tempB1, tempF1);                     \
        tempE2 = vec_mergeh (tempC1, tempG1);                     \
        tempF2 = vec_mergel (tempC1, tempG1);                     \
        tempG2 = vec_mergeh (tempD1, tempH1);                     \
        tempH2 = vec_mergel (tempD1, tempH1);                     \
        src_a = vec_mergeh (tempA2, tempE2);                      \
        src_b = vec_mergel (tempA2, tempE2);                      \
        src_c = vec_mergeh (tempB2, tempF2);                      \
        src_d = vec_mergel (tempB2, tempF2);                      \
        src_e = vec_mergeh (tempC2, tempG2);                      \
        src_f = vec_mergel (tempC2, tempG2);                      \
        src_g = vec_mergeh (tempD2, tempH2);                      \
        src_h = vec_mergel (tempD2, tempH2);                      \
    } while (0)


static inline int vertClassify_altivec(uint8_t src[], int stride, PPContext *c) {
    /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true.
    */
    DECLARE_ALIGNED(16, short, data[8]) =
                    {
                        ((c->nonBQP*c->ppMode.baseDcDiff)>>8) + 1,
                        data[0] * 2 + 1,
                        c->QP * 2,
                        c->QP * 4
                    };
    int numEq;
    uint8_t *src2 = src;
    vector signed short v_dcOffset;
    vector signed short v2QP;
    vector unsigned short v4QP;
    vector unsigned short v_dcThreshold;
    const int properStride = (stride % 16);
    const int srcAlign = ((unsigned long)src2 % 16);
    const int two_vectors = ((srcAlign > 8) || properStride) ? 1 : 0;
    const vector signed int zero = vec_splat_s32(0);
    const vector signed short mask = vec_splat_s16(1);
    vector signed int v_numEq = vec_splat_s32(0);
    vector signed short v_data = vec_ld(0, data);
    vector signed short v_srcAss0, v_srcAss1, v_srcAss2, v_srcAss3,
                        v_srcAss4, v_srcAss5, v_srcAss6, v_srcAss7;
//FIXME avoid this mess if possible
    register int j0 = 0,
                 j1 = stride,
                 j2 = 2 * stride,
                 j3 = 3 * stride,
                 j4 = 4 * stride,
                 j5 = 5 * stride,
                 j6 = 6 * stride,
                 j7 = 7 * stride;
    vector unsigned char v_srcA0, v_srcA1, v_srcA2, v_srcA3,
                         v_srcA4, v_srcA5, v_srcA6, v_srcA7;

    v_dcOffset = vec_splat(v_data, 0);
    v_dcThreshold = (vector unsigned short)vec_splat(v_data, 1);
    v2QP = vec_splat(v_data, 2);
    v4QP = (vector unsigned short)vec_splat(v_data, 3);

    src2 += stride * 4;

#define LOAD_LINE(i)                                                    \
    {                                                                   \
    vector unsigned char perm##i = vec_lvsl(j##i, src2);                \
    vector unsigned char v_srcA2##i;                                    \
    vector unsigned char v_srcA1##i = vec_ld(j##i, src2);               \
    if (two_vectors)                                                    \
        v_srcA2##i = vec_ld(j##i + 16, src2);                           \
    v_srcA##i =                                                         \
        vec_perm(v_srcA1##i, v_srcA2##i, perm##i);                      \
    v_srcAss##i =                                                       \
        (vector signed short)vec_mergeh((vector signed char)zero,       \
                                        (vector signed char)v_srcA##i); }

#define LOAD_LINE_ALIGNED(i)                                            \
    v_srcA##i = vec_ld(j##i, src2);                                     \
    v_srcAss##i =                                                       \
        (vector signed short)vec_mergeh((vector signed char)zero,       \
                                        (vector signed char)v_srcA##i)

    /* Special-casing the aligned case is worthwhile, as all calls from
     * the (transposed) horizontable deblocks will be aligned, in addition
     * to the naturally aligned vertical deblocks. */
    if (properStride && srcAlign) {
        LOAD_LINE_ALIGNED(0);
        LOAD_LINE_ALIGNED(1);
        LOAD_LINE_ALIGNED(2);
        LOAD_LINE_ALIGNED(3);
        LOAD_LINE_ALIGNED(4);
        LOAD_LINE_ALIGNED(5);
        LOAD_LINE_ALIGNED(6);
        LOAD_LINE_ALIGNED(7);
    } else {
        LOAD_LINE(0);
        LOAD_LINE(1);
        LOAD_LINE(2);
        LOAD_LINE(3);
        LOAD_LINE(4);
        LOAD_LINE(5);
        LOAD_LINE(6);
        LOAD_LINE(7);
    }
#undef LOAD_LINE
#undef LOAD_LINE_ALIGNED

#define ITER(i, j)                                                      \
    const vector signed short v_diff##i =                               \
        vec_sub(v_srcAss##i, v_srcAss##j);                              \
    const vector signed short v_sum##i =                                \
        vec_add(v_diff##i, v_dcOffset);                                 \
    const vector signed short v_comp##i =                               \
        (vector signed short)vec_cmplt((vector unsigned short)v_sum##i, \
                                       v_dcThreshold);                  \
    const vector signed short v_part##i = vec_and(mask, v_comp##i);

    {
        ITER(0, 1)
        ITER(1, 2)
        ITER(2, 3)
        ITER(3, 4)
        ITER(4, 5)
        ITER(5, 6)
        ITER(6, 7)

        v_numEq = vec_sum4s(v_part0, v_numEq);
        v_numEq = vec_sum4s(v_part1, v_numEq);
        v_numEq = vec_sum4s(v_part2, v_numEq);
        v_numEq = vec_sum4s(v_part3, v_numEq);
        v_numEq = vec_sum4s(v_part4, v_numEq);
        v_numEq = vec_sum4s(v_part5, v_numEq);
        v_numEq = vec_sum4s(v_part6, v_numEq);
    }

#undef ITER

    v_numEq = vec_sums(v_numEq, zero);

    v_numEq = vec_splat(v_numEq, 3);
    vec_ste(v_numEq, 0, &numEq);

    if (numEq > c->ppMode.flatnessThreshold){
        const vector unsigned char mmoP1 = (const vector unsigned char)
            {0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f,
             0x00, 0x01, 0x12, 0x13, 0x08, 0x09, 0x1A, 0x1B};
        const vector unsigned char mmoP2 = (const vector unsigned char)
            {0x04, 0x05, 0x16, 0x17, 0x0C, 0x0D, 0x1E, 0x1F,
             0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
        const vector unsigned char mmoP = (const vector unsigned char)
            vec_lvsl(8, (unsigned char*)0);

        vector signed short mmoL1 = vec_perm(v_srcAss0, v_srcAss2, mmoP1);
        vector signed short mmoL2 = vec_perm(v_srcAss4, v_srcAss6, mmoP2);
        vector signed short mmoL = vec_perm(mmoL1, mmoL2, mmoP);
        vector signed short mmoR1 = vec_perm(v_srcAss5, v_srcAss7, mmoP1);
        vector signed short mmoR2 = vec_perm(v_srcAss1, v_srcAss3, mmoP2);
        vector signed short mmoR = vec_perm(mmoR1, mmoR2, mmoP);
        vector signed short mmoDiff = vec_sub(mmoL, mmoR);
        vector unsigned short mmoSum = (vector unsigned short)vec_add(mmoDiff, v2QP);

        if (vec_any_gt(mmoSum, v4QP))
            return 0;
        else
            return 1;
    }
    else return 2;
}

static inline void doVertLowPass_altivec(uint8_t *src, int stride, PPContext *c) {
    /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignment of
    src & stride :-(
    */
    uint8_t *src2 = src;
    const vector signed int zero = vec_splat_s32(0);
    const int properStride = (stride % 16);
    const int srcAlign = ((unsigned long)src2 % 16);
    DECLARE_ALIGNED(16, short, qp[8]) = {c->QP};
    vector signed short vqp = vec_ld(0, qp);
    vector signed short vb0, vb1, vb2, vb3, vb4, vb5, vb6, vb7, vb8, vb9;
    vector unsigned char vbA0, vbA1, vbA2, vbA3, vbA4, vbA5, vbA6, vbA7, vbA8, vbA9;
    vector unsigned char vbB0, vbB1, vbB2, vbB3, vbB4, vbB5, vbB6, vbB7, vbB8, vbB9;
    vector unsigned char vbT0, vbT1, vbT2, vbT3, vbT4, vbT5, vbT6, vbT7, vbT8, vbT9;
    vector unsigned char perml0, perml1, perml2, perml3, perml4,
                         perml5, perml6, perml7, perml8, perml9;
    register int j0 = 0,
                 j1 = stride,
                 j2 = 2 * stride,
                 j3 = 3 * stride,
                 j4 = 4 * stride,
                 j5 = 5 * stride,
                 j6 = 6 * stride,
                 j7 = 7 * stride,
                 j8 = 8 * stride,
                 j9 = 9 * stride;

    vqp = vec_splat(vqp, 0);

    src2 += stride*3;

#define LOAD_LINE(i)                                                    \
    perml##i = vec_lvsl(i * stride, src2);                              \
    vbA##i = vec_ld(i * stride, src2);                                  \
    vbB##i = vec_ld(i * stride + 16, src2);                             \
    vbT##i = vec_perm(vbA##i, vbB##i, perml##i);                        \
    vb##i =                                                             \
        (vector signed short)vec_mergeh((vector unsigned char)zero,     \
                                        (vector unsigned char)vbT##i)

#define LOAD_LINE_ALIGNED(i)                                            \
    vbT##i = vec_ld(j##i, src2);                                        \
    vb##i =                                                             \
        (vector signed short)vec_mergeh((vector signed char)zero,       \
                                        (vector signed char)vbT##i)

      /* Special-casing the aligned case is worthwhile, as all calls from
       * the (transposed) horizontable deblocks will be aligned, in addition
       * to the naturally aligned vertical deblocks. */
    if (properStride && srcAlign) {
          LOAD_LINE_ALIGNED(0);
          LOAD_LINE_ALIGNED(1);
          LOAD_LINE_ALIGNED(2);
          LOAD_LINE_ALIGNED(3);
          LOAD_LINE_ALIGNED(4);
          LOAD_LINE_ALIGNED(5);
          LOAD_LINE_ALIGNED(6);
          LOAD_LINE_ALIGNED(7);
          LOAD_LINE_ALIGNED(8);
          LOAD_LINE_ALIGNED(9);
    } else {
          LOAD_LINE(0);
          LOAD_LINE(1);
          LOAD_LINE(2);
          LOAD_LINE(3);
          LOAD_LINE(4);
          LOAD_LINE(5);
          LOAD_LINE(6);
          LOAD_LINE(7);
          LOAD_LINE(8);
          LOAD_LINE(9);
    }
#undef LOAD_LINE
#undef LOAD_LINE_ALIGNED
    {
        const vector unsigned short v_2 = vec_splat_u16(2);
        const vector unsigned short v_4 = vec_splat_u16(4);

        const vector signed short v_diff01 = vec_sub(vb0, vb1);
        const vector unsigned short v_cmp01 =
            (const vector unsigned short) vec_cmplt(vec_abs(v_diff01), vqp);
        const vector signed short v_first = vec_sel(vb1, vb0, v_cmp01);
        const vector signed short v_diff89 = vec_sub(vb8, vb9);
        const vector unsigned short v_cmp89 =
            (const vector unsigned short) vec_cmplt(vec_abs(v_diff89), vqp);
        const vector signed short v_last = vec_sel(vb8, vb9, v_cmp89);

        const vector signed short temp01 = vec_mladd(v_first, (vector signed short)v_4, vb1);
        const vector signed short temp02 = vec_add(vb2, vb3);
        const vector signed short temp03 = vec_add(temp01, (vector signed short)v_4);
        const vector signed short v_sumsB0 = vec_add(temp02, temp03);

        const vector signed short temp11 = vec_sub(v_sumsB0, v_first);
        const vector signed short v_sumsB1 = vec_add(temp11, vb4);

        const vector signed short temp21 = vec_sub(v_sumsB1, v_first);
        const vector signed short v_sumsB2 = vec_add(temp21, vb5);

        const vector signed short temp31 = vec_sub(v_sumsB2, v_first);
        const vector signed short v_sumsB3 = vec_add(temp31, vb6);

        const vector signed short temp41 = vec_sub(v_sumsB3, v_first);
        const vector signed short v_sumsB4 = vec_add(temp41, vb7);

        const vector signed short temp51 = vec_sub(v_sumsB4, vb1);
        const vector signed short v_sumsB5 = vec_add(temp51, vb8);

        const vector signed short temp61 = vec_sub(v_sumsB5, vb2);
        const vector signed short v_sumsB6 = vec_add(temp61, v_last);

        const vector signed short temp71 = vec_sub(v_sumsB6, vb3);
        const vector signed short v_sumsB7 = vec_add(temp71, v_last);

        const vector signed short temp81 = vec_sub(v_sumsB7, vb4);
        const vector signed short v_sumsB8 = vec_add(temp81, v_last);

        const vector signed short temp91 = vec_sub(v_sumsB8, vb5);
        const vector signed short v_sumsB9 = vec_add(temp91, v_last);

    #define COMPUTE_VR(i, j, k)                                             \
        const vector signed short temps1##i =                               \
            vec_add(v_sumsB##i, v_sumsB##k);                                \
        const vector signed short temps2##i =                               \
            vec_mladd(vb##j, (vector signed short)v_2, temps1##i);          \
        const vector signed short  vr##j = vec_sra(temps2##i, v_4)

        COMPUTE_VR(0, 1, 2);
        COMPUTE_VR(1, 2, 3);
        COMPUTE_VR(2, 3, 4);
        COMPUTE_VR(3, 4, 5);
        COMPUTE_VR(4, 5, 6);
        COMPUTE_VR(5, 6, 7);
        COMPUTE_VR(6, 7, 8);
        COMPUTE_VR(7, 8, 9);

        const vector signed char neg1 = vec_splat_s8(-1);
        const vector unsigned char permHH = (const vector unsigned char){0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                                                         0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

#define PACK_AND_STORE(i)                                       \
{   const vector unsigned char perms##i =                       \
        vec_lvsr(i * stride, src2);                             \
    const vector unsigned char vf##i =                          \
        vec_packsu(vr##i, (vector signed short)zero);           \
    const vector unsigned char vg##i =                          \
        vec_perm(vf##i, vbT##i, permHH);                        \
    const vector unsigned char mask##i =                        \
        vec_perm((vector unsigned char)zero, (vector unsigned char)neg1, perms##i); \
    const vector unsigned char vg2##i =                         \
        vec_perm(vg##i, vg##i, perms##i);                       \
    const vector unsigned char svA##i =                         \
        vec_sel(vbA##i, vg2##i, mask##i);                       \
    const vector unsigned char svB##i =                         \
        vec_sel(vg2##i, vbB##i, mask##i);                       \
    vec_st(svA##i, i * stride, src2);                           \
    vec_st(svB##i, i * stride + 16, src2);}

#define PACK_AND_STORE_ALIGNED(i)                               \
{   const vector unsigned char vf##i =                          \
        vec_packsu(vr##i, (vector signed short)zero);           \
    const vector unsigned char vg##i =                          \
        vec_perm(vf##i, vbT##i, permHH);                        \
    vec_st(vg##i, i * stride, src2);}

        /* Special-casing the aligned case is worthwhile, as all calls from
         * the (transposed) horizontable deblocks will be aligned, in addition
         * to the naturally aligned vertical deblocks. */
        if (properStride && srcAlign) {
            PACK_AND_STORE_ALIGNED(1)
            PACK_AND_STORE_ALIGNED(2)
            PACK_AND_STORE_ALIGNED(3)
            PACK_AND_STORE_ALIGNED(4)
            PACK_AND_STORE_ALIGNED(5)
            PACK_AND_STORE_ALIGNED(6)
            PACK_AND_STORE_ALIGNED(7)
            PACK_AND_STORE_ALIGNED(8)
        } else {
            PACK_AND_STORE(1)
            PACK_AND_STORE(2)
            PACK_AND_STORE(3)
            PACK_AND_STORE(4)
            PACK_AND_STORE(5)
            PACK_AND_STORE(6)
            PACK_AND_STORE(7)
            PACK_AND_STORE(8)
        }
    #undef PACK_AND_STORE
    #undef PACK_AND_STORE_ALIGNED
    }
}



static inline void doVertDefFilter_altivec(uint8_t src[], int stride, PPContext *c) {
    /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignment of
    src & stride :-(
    */
    uint8_t *src2 = src + stride*3;
    const vector signed int zero = vec_splat_s32(0);
    DECLARE_ALIGNED(16, short, qp[8]) = {8*c->QP};
    vector signed short vqp = vec_splat(
                                (vector signed short)vec_ld(0, qp), 0);

#define LOAD_LINE(i)                                                    \
    const vector unsigned char perm##i =                                \
        vec_lvsl(i * stride, src2);                                     \
    const vector unsigned char vbA##i =                                 \
        vec_ld(i * stride, src2);                                       \
    const vector unsigned char vbB##i =                                 \
        vec_ld(i * stride + 16, src2);                                  \
    const vector unsigned char vbT##i =                                 \
        vec_perm(vbA##i, vbB##i, perm##i);                              \
    const vector signed short vb##i =                                   \
        (vector signed short)vec_mergeh((vector unsigned char)zero,     \
                                        (vector unsigned char)vbT##i)

     LOAD_LINE(1);
     LOAD_LINE(2);
     LOAD_LINE(3);
     LOAD_LINE(4);
     LOAD_LINE(5);
     LOAD_LINE(6);
     LOAD_LINE(7);
     LOAD_LINE(8);
#undef LOAD_LINE

     const vector signed short v_1 = vec_splat_s16(1);
     const vector signed short v_2 = vec_splat_s16(2);
     const vector signed short v_5 = vec_splat_s16(5);
     const vector signed short v_32 = vec_sl(v_1,
                                             (vector unsigned short)v_5);
     /* middle energy */
     const vector signed short l3minusl6 = vec_sub(vb3, vb6);
     const vector signed short l5minusl4 = vec_sub(vb5, vb4);
     const vector signed short twotimes_l3minusl6 = vec_mladd(v_2, l3minusl6, (vector signed short)zero);
     const vector signed short mE = vec_mladd(v_5, l5minusl4, twotimes_l3minusl6);
     const vector signed short absmE = vec_abs(mE);
     /* left & right energy */
     const vector signed short l1minusl4 = vec_sub(vb1, vb4);
     const vector signed short l3minusl2 = vec_sub(vb3, vb2);
     const vector signed short l5minusl8 = vec_sub(vb5, vb8);
     const vector signed short l7minusl6 = vec_sub(vb7, vb6);
     const vector signed short twotimes_l1minusl4 = vec_mladd(v_2, l1minusl4, (vector signed short)zero);
     const vector signed short twotimes_l5minusl8 = vec_mladd(v_2, l5minusl8, (vector signed short)zero);
     const vector signed short lE = vec_mladd(v_5, l3minusl2, twotimes_l1minusl4);
     const vector signed short rE = vec_mladd(v_5, l7minusl6, twotimes_l5minusl8);
     /* d */
     const vector signed short ddiff = vec_sub(absmE,
                                               vec_min(vec_abs(lE),
                                                       vec_abs(rE)));
     const vector signed short ddiffclamp = vec_max(ddiff, (vector signed short)zero);
     const vector signed short dtimes64 = vec_mladd(v_5, ddiffclamp, v_32);
     const vector signed short d = vec_sra(dtimes64, vec_splat_u16(6));
     const vector signed short minusd = vec_sub((vector signed short)zero, d);
     const vector signed short finald = vec_sel(minusd,
                                                d,
                                                vec_cmpgt(vec_sub((vector signed short)zero, mE),
                                                          (vector signed short)zero));
     /* q */
     const vector signed short qtimes2 = vec_sub(vb4, vb5);
     /* for a shift right to behave like /2, we need to add one
        to all negative integer */
     const vector signed short rounddown = vec_sel((vector signed short)zero,
                                                   v_1,
                                                   vec_cmplt(qtimes2, (vector signed short)zero));
     const vector signed short q = vec_sra(vec_add(qtimes2, rounddown), vec_splat_u16(1));
     /* clamp */
     const vector signed short dclamp_P1 = vec_max((vector signed short)zero, finald);
     const vector signed short dclamp_P = vec_min(dclamp_P1, q);
     const vector signed short dclamp_N1 = vec_min((vector signed short)zero, finald);
     const vector signed short dclamp_N = vec_max(dclamp_N1, q);

     const vector signed short dclampedfinal = vec_sel(dclamp_N,
                                                       dclamp_P,
                                                       vec_cmpgt(q, (vector signed short)zero));
     const vector signed short dornotd = vec_sel((vector signed short)zero,
                                                 dclampedfinal,
                                                 vec_cmplt(absmE, vqp));
     /* add/subtract to l4 and l5 */
     const vector signed short vb4minusd = vec_sub(vb4, dornotd);
     const vector signed short vb5plusd  = vec_add(vb5, dornotd);
     /* finally, stores */
     const vector unsigned char st4 = vec_packsu(vb4minusd, (vector signed short)zero);
     const vector unsigned char st5 = vec_packsu(vb5plusd,  (vector signed short)zero);

     const vector signed char neg1 = vec_splat_s8(-1);
     const vector unsigned char permHH = (const vector unsigned char){0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                                                      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

#define STORE(i)                                                \
{    const vector unsigned char perms##i =                      \
         vec_lvsr(i * stride, src2);                            \
     const vector unsigned char vg##i =                         \
         vec_perm(st##i, vbT##i, permHH);                       \
     const vector unsigned char mask##i =                       \
         vec_perm((vector unsigned char)zero, (vector unsigned char)neg1, perms##i); \
     const vector unsigned char vg2##i =                        \
         vec_perm(vg##i, vg##i, perms##i);                      \
     const vector unsigned char svA##i =                        \
         vec_sel(vbA##i, vg2##i, mask##i);                      \
     const vector unsigned char svB##i =                        \
         vec_sel(vg2##i, vbB##i, mask##i);                      \
     vec_st(svA##i, i * stride, src2);                          \
     vec_st(svB##i, i * stride + 16, src2);}

     STORE(4)
     STORE(5)
}

static inline void dering_altivec(uint8_t src[], int stride, PPContext *c) {
    /*
    this code makes no assumption on src or stride.
    One could remove the recomputation of the perm
    vector by assuming (stride % 16) == 0, unfortunately
    this is not always true. Quite a lot of load/stores
    can be removed by assuming proper alignment of
    src & stride :-(
    */
    uint8_t *srcCopy = src;
    DECLARE_ALIGNED(16, uint8_t, dt[16]);
    const vector signed int zero = vec_splat_s32(0);
    vector unsigned char v_dt;
    dt[0] = deringThreshold;
    v_dt = vec_splat(vec_ld(0, dt), 0);

#define LOAD_LINE(i)                                                  \
    const vector unsigned char perm##i =                              \
        vec_lvsl(i * stride, srcCopy);                                \
    vector unsigned char sA##i = vec_ld(i * stride, srcCopy);         \
    vector unsigned char sB##i = vec_ld(i * stride + 16, srcCopy);    \
    vector unsigned char src##i = vec_perm(sA##i, sB##i, perm##i)

    LOAD_LINE(0);
    LOAD_LINE(1);
    LOAD_LINE(2);
    LOAD_LINE(3);
    LOAD_LINE(4);
    LOAD_LINE(5);
    LOAD_LINE(6);
    LOAD_LINE(7);
    LOAD_LINE(8);
    LOAD_LINE(9);
#undef LOAD_LINE

    vector unsigned char v_avg;
    {
    const vector unsigned char trunc_perm = (vector unsigned char)
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
         0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18};
    const vector unsigned char trunc_src12 = vec_perm(src1, src2, trunc_perm);
    const vector unsigned char trunc_src34 = vec_perm(src3, src4, trunc_perm);
    const vector unsigned char trunc_src56 = vec_perm(src5, src6, trunc_perm);
    const vector unsigned char trunc_src78 = vec_perm(src7, src8, trunc_perm);

#define EXTRACT(op) do {                                                \
    const vector unsigned char s##op##_1   = vec_##op(trunc_src12, trunc_src34); \
    const vector unsigned char s##op##_2   = vec_##op(trunc_src56, trunc_src78); \
    const vector unsigned char s##op##_6   = vec_##op(s##op##_1, s##op##_2);     \
    const vector unsigned char s##op##_8h  = vec_mergeh(s##op##_6, s##op##_6);   \
    const vector unsigned char s##op##_8l  = vec_mergel(s##op##_6, s##op##_6);   \
    const vector unsigned char s##op##_9   = vec_##op(s##op##_8h, s##op##_8l);   \
    const vector unsigned char s##op##_9h  = vec_mergeh(s##op##_9, s##op##_9);   \
    const vector unsigned char s##op##_9l  = vec_mergel(s##op##_9, s##op##_9);   \
    const vector unsigned char s##op##_10  = vec_##op(s##op##_9h, s##op##_9l);   \
    const vector unsigned char s##op##_10h = vec_mergeh(s##op##_10, s##op##_10); \
    const vector unsigned char s##op##_10l = vec_mergel(s##op##_10, s##op##_10); \
    const vector unsigned char s##op##_11  = vec_##op(s##op##_10h, s##op##_10l); \
    const vector unsigned char s##op##_11h = vec_mergeh(s##op##_11, s##op##_11); \
    const vector unsigned char s##op##_11l = vec_mergel(s##op##_11, s##op##_11); \
    v_##op = vec_##op(s##op##_11h, s##op##_11l); } while (0)

    vector unsigned char v_min;
    vector unsigned char v_max;
    EXTRACT(min);
    EXTRACT(max);
#undef EXTRACT

    if (vec_all_lt(vec_sub(v_max, v_min), v_dt))
        return;

    v_avg = vec_avg(v_min, v_max);
    }

    DECLARE_ALIGNED(16, signed int, S[8]);
    {
    const vector unsigned short mask1 = (vector unsigned short)
                                        {0x0001, 0x0002, 0x0004, 0x0008,
                                         0x0010, 0x0020, 0x0040, 0x0080};
    const vector unsigned short mask2 = (vector unsigned short)
                                        {0x0100, 0x0200, 0x0000, 0x0000,
                                         0x0000, 0x0000, 0x0000, 0x0000};

    const vector unsigned int vuint32_16 = vec_sl(vec_splat_u32(1), vec_splat_u32(4));
    const vector unsigned int vuint32_1 = vec_splat_u32(1);

#define COMPARE(i)                                                      \
    vector signed int sum##i;                                           \
    do {                                                                \
        const vector unsigned char cmp##i =                             \
            (vector unsigned char)vec_cmpgt(src##i, v_avg);             \
        const vector unsigned short cmpHi##i =                          \
            (vector unsigned short)vec_mergeh(cmp##i, cmp##i);          \
        const vector unsigned short cmpLi##i =                          \
            (vector unsigned short)vec_mergel(cmp##i, cmp##i);          \
        const vector signed short cmpHf##i =                            \
            (vector signed short)vec_and(cmpHi##i, mask1);              \
        const vector signed short cmpLf##i =                            \
            (vector signed short)vec_and(cmpLi##i, mask2);              \
        const vector signed int sump##i = vec_sum4s(cmpHf##i, zero);    \
        const vector signed int sumq##i = vec_sum4s(cmpLf##i, sump##i); \
        sum##i  = vec_sums(sumq##i, zero); } while (0)

    COMPARE(0);
    COMPARE(1);
    COMPARE(2);
    COMPARE(3);
    COMPARE(4);
    COMPARE(5);
    COMPARE(6);
    COMPARE(7);
    COMPARE(8);
    COMPARE(9);
#undef COMPARE

    vector signed int sumA2;
    vector signed int sumB2;
    {
    const vector signed int sump02 = vec_mergel(sum0, sum2);
    const vector signed int sump13 = vec_mergel(sum1, sum3);
    const vector signed int sumA = vec_mergel(sump02, sump13);

    const vector signed int sump46 = vec_mergel(sum4, sum6);
    const vector signed int sump57 = vec_mergel(sum5, sum7);
    const vector signed int sumB = vec_mergel(sump46, sump57);

    const vector signed int sump8A = vec_mergel(sum8, zero);
    const vector signed int sump9B = vec_mergel(sum9, zero);
    const vector signed int sumC = vec_mergel(sump8A, sump9B);

    const vector signed int tA = vec_sl(vec_nor(zero, sumA), vuint32_16);
    const vector signed int tB = vec_sl(vec_nor(zero, sumB), vuint32_16);
    const vector signed int tC = vec_sl(vec_nor(zero, sumC), vuint32_16);
    const vector signed int t2A = vec_or(sumA, tA);
    const vector signed int t2B = vec_or(sumB, tB);
    const vector signed int t2C = vec_or(sumC, tC);
    const vector signed int t3A = vec_and(vec_sra(t2A, vuint32_1),
                                          vec_sl(t2A, vuint32_1));
    const vector signed int t3B = vec_and(vec_sra(t2B, vuint32_1),
                                          vec_sl(t2B, vuint32_1));
    const vector signed int t3C = vec_and(vec_sra(t2C, vuint32_1),
                                          vec_sl(t2C, vuint32_1));
    const vector signed int yA = vec_and(t2A, t3A);
    const vector signed int yB = vec_and(t2B, t3B);
    const vector signed int yC = vec_and(t2C, t3C);

    const vector unsigned char strangeperm1 = vec_lvsl(4, (unsigned char*)0);
    const vector unsigned char strangeperm2 = vec_lvsl(8, (unsigned char*)0);
    const vector signed int sumAd4 = vec_perm(yA, yB, strangeperm1);
    const vector signed int sumAd8 = vec_perm(yA, yB, strangeperm2);
    const vector signed int sumBd4 = vec_perm(yB, yC, strangeperm1);
    const vector signed int sumBd8 = vec_perm(yB, yC, strangeperm2);
    const vector signed int sumAp = vec_and(yA,
                                            vec_and(sumAd4,sumAd8));
    const vector signed int sumBp = vec_and(yB,
                                            vec_and(sumBd4,sumBd8));
    sumA2 = vec_or(sumAp,
                   vec_sra(sumAp,
                           vuint32_16));
    sumB2  = vec_or(sumBp,
                    vec_sra(sumBp,
                            vuint32_16));
    }
    vec_st(sumA2, 0, S);
    vec_st(sumB2, 16, S);
    }

    /* I'm not sure the following is actually faster
       than straight, unvectorized C code :-( */

    DECLARE_ALIGNED(16, int, tQP2[4]);
    tQP2[0]= c->QP/2 + 1;
    vector signed int vQP2 = vec_ld(0, tQP2);
    vQP2 = vec_splat(vQP2, 0);
    const vector signed int vsint32_8 = vec_splat_s32(8);
    const vector unsigned int vuint32_4 = vec_splat_u32(4);

    const vector unsigned char permA1 = (vector unsigned char)
        {0x00, 0x01, 0x02, 0x10, 0x11, 0x12, 0x1F, 0x1F,
         0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    const vector unsigned char permA2 = (vector unsigned char)
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11,
         0x12, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F};
    const vector unsigned char permA1inc = (vector unsigned char)
        {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const vector unsigned char permA2inc = (vector unsigned char)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
         0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const vector unsigned char magic = (vector unsigned char)
        {0x01, 0x02, 0x01, 0x02, 0x04, 0x02, 0x01, 0x02,
         0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const vector unsigned char extractPerm = (vector unsigned char)
        {0x10, 0x10, 0x10, 0x01, 0x10, 0x10, 0x10, 0x01,
         0x10, 0x10, 0x10, 0x01, 0x10, 0x10, 0x10, 0x01};
    const vector unsigned char extractPermInc = (vector unsigned char)
        {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01};
    const vector unsigned char identity = vec_lvsl(0,(unsigned char *)0);
    const vector unsigned char tenRight = (vector unsigned char)
        {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const vector unsigned char eightLeft = (vector unsigned char)
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08};


#define F_INIT(i)                                       \
    vector unsigned char tenRightM##i = tenRight;       \
    vector unsigned char permA1M##i = permA1;           \
    vector unsigned char permA2M##i = permA2;           \
    vector unsigned char extractPermM##i = extractPerm

#define F2(i, j, k, l)                                                  \
    if (S[i] & (1 << (l+1))) {                                          \
        const vector unsigned char a_##j##_A##l =                       \
            vec_perm(src##i, src##j, permA1M##i);                       \
        const vector unsigned char a_##j##_B##l =                       \
            vec_perm(a_##j##_A##l, src##k, permA2M##i);                 \
        const vector signed int a_##j##_sump##l =                       \
            (vector signed int)vec_msum(a_##j##_B##l, magic,            \
                                        (vector unsigned int)zero);     \
        vector signed int F_##j##_##l =                                 \
            vec_sr(vec_sums(a_##j##_sump##l, vsint32_8), vuint32_4);    \
        F_##j##_##l = vec_splat(F_##j##_##l, 3);                        \
        const vector signed int p_##j##_##l =                           \
            (vector signed int)vec_perm(src##j,                         \
                                        (vector unsigned char)zero,     \
                                        extractPermM##i);               \
        const vector signed int sum_##j##_##l  = vec_add( p_##j##_##l, vQP2);\
        const vector signed int diff_##j##_##l = vec_sub( p_##j##_##l, vQP2);\
        vector signed int newpm_##j##_##l;                              \
        if (vec_all_lt(sum_##j##_##l, F_##j##_##l))                     \
            newpm_##j##_##l = sum_##j##_##l;                            \
        else if (vec_all_gt(diff_##j##_##l, F_##j##_##l))               \
            newpm_##j##_##l = diff_##j##_##l;                           \
        else newpm_##j##_##l = F_##j##_##l;                             \
        const vector unsigned char newpm2_##j##_##l =                   \
            vec_splat((vector unsigned char)newpm_##j##_##l, 15);       \
        const vector unsigned char mask##j##l = vec_add(identity,       \
                                                        tenRightM##i);  \
        src##j = vec_perm(src##j, newpm2_##j##_##l, mask##j##l);        \
    }                                                                   \
    permA1M##i = vec_add(permA1M##i, permA1inc);                        \
    permA2M##i = vec_add(permA2M##i, permA2inc);                        \
    tenRightM##i = vec_sro(tenRightM##i, eightLeft);                    \
    extractPermM##i = vec_add(extractPermM##i, extractPermInc)

#define ITER(i, j, k)                           \
    F_INIT(i);                                  \
    F2(i, j, k, 0);                             \
    F2(i, j, k, 1);                             \
    F2(i, j, k, 2);                             \
    F2(i, j, k, 3);                             \
    F2(i, j, k, 4);                             \
    F2(i, j, k, 5);                             \
    F2(i, j, k, 6);                             \
    F2(i, j, k, 7)

    ITER(0, 1, 2);
    ITER(1, 2, 3);
    ITER(2, 3, 4);
    ITER(3, 4, 5);
    ITER(4, 5, 6);
    ITER(5, 6, 7);
    ITER(6, 7, 8);
    ITER(7, 8, 9);

    const vector signed char neg1 = vec_splat_s8(-1);

#define STORE_LINE(i)                                   \
    const vector unsigned char permST##i =              \
        vec_lvsr(i * stride, srcCopy);                  \
    const vector unsigned char maskST##i =              \
        vec_perm((vector unsigned char)zero,            \
                 (vector unsigned char)neg1, permST##i);\
    src##i = vec_perm(src##i ,src##i, permST##i);       \
    sA##i= vec_sel(sA##i, src##i, maskST##i);           \
    sB##i= vec_sel(src##i, sB##i, maskST##i);           \
    vec_st(sA##i, i * stride, srcCopy);                 \
    vec_st(sB##i, i * stride + 16, srcCopy)

    STORE_LINE(1);
    STORE_LINE(2);
    STORE_LINE(3);
    STORE_LINE(4);
    STORE_LINE(5);
    STORE_LINE(6);
    STORE_LINE(7);
    STORE_LINE(8);

#undef STORE_LINE
#undef ITER
#undef F2
}

#define doHorizLowPass_altivec(a...) doHorizLowPass_C(a)
#define doHorizDefFilter_altivec(a...) doHorizDefFilter_C(a)
#define do_a_deblock_altivec(a...) do_a_deblock_C(a)

static inline void RENAME(tempNoiseReducer)(uint8_t *src, int stride,
                                            uint8_t *tempBlurred, uint32_t *tempBlurredPast, int *maxNoise)
{
    const vector signed int zero = vec_splat_s32(0);
    const vector signed short vsint16_1 = vec_splat_s16(1);
    vector signed int v_dp = zero;
    vector signed int v_sysdp = zero;
    int d, sysd, i;

    tempBlurredPast[127]= maxNoise[0];
    tempBlurredPast[128]= maxNoise[1];
    tempBlurredPast[129]= maxNoise[2];

#define LOAD_LINE(src, i)                                               \
    register int j##src##i = i * stride;                                \
    vector unsigned char perm##src##i = vec_lvsl(j##src##i, src);       \
    const vector unsigned char v_##src##A1##i = vec_ld(j##src##i, src); \
    const vector unsigned char v_##src##A2##i = vec_ld(j##src##i + 16, src); \
    const vector unsigned char v_##src##A##i =                          \
        vec_perm(v_##src##A1##i, v_##src##A2##i, perm##src##i);         \
    vector signed short v_##src##Ass##i =                               \
        (vector signed short)vec_mergeh((vector signed char)zero,       \
                                        (vector signed char)v_##src##A##i)

    LOAD_LINE(src, 0);
    LOAD_LINE(src, 1);
    LOAD_LINE(src, 2);
    LOAD_LINE(src, 3);
    LOAD_LINE(src, 4);
    LOAD_LINE(src, 5);
    LOAD_LINE(src, 6);
    LOAD_LINE(src, 7);

    LOAD_LINE(tempBlurred, 0);
    LOAD_LINE(tempBlurred, 1);
    LOAD_LINE(tempBlurred, 2);
    LOAD_LINE(tempBlurred, 3);
    LOAD_LINE(tempBlurred, 4);
    LOAD_LINE(tempBlurred, 5);
    LOAD_LINE(tempBlurred, 6);
    LOAD_LINE(tempBlurred, 7);
#undef LOAD_LINE

#define ACCUMULATE_DIFFS(i)                                     \
    vector signed short v_d##i = vec_sub(v_tempBlurredAss##i,   \
                                         v_srcAss##i);          \
    v_dp = vec_msums(v_d##i, v_d##i, v_dp);                     \
    v_sysdp = vec_msums(v_d##i, vsint16_1, v_sysdp)

    ACCUMULATE_DIFFS(0);
    ACCUMULATE_DIFFS(1);
    ACCUMULATE_DIFFS(2);
    ACCUMULATE_DIFFS(3);
    ACCUMULATE_DIFFS(4);
    ACCUMULATE_DIFFS(5);
    ACCUMULATE_DIFFS(6);
    ACCUMULATE_DIFFS(7);
#undef ACCUMULATE_DIFFS

    v_dp = vec_sums(v_dp, zero);
    v_sysdp = vec_sums(v_sysdp, zero);

    v_dp = vec_splat(v_dp, 3);
    v_sysdp = vec_splat(v_sysdp, 3);

    vec_ste(v_dp, 0, &d);
    vec_ste(v_sysdp, 0, &sysd);

    i = d;
    d = (4*d
         +(*(tempBlurredPast-256))
         +(*(tempBlurredPast-1))+ (*(tempBlurredPast+1))
         +(*(tempBlurredPast+256))
         +4)>>3;

    *tempBlurredPast=i;

    if (d > maxNoise[1]) {
        if (d < maxNoise[2]) {
#define OP(i) v_tempBlurredAss##i = vec_avg(v_tempBlurredAss##i, v_srcAss##i);

            OP(0);
            OP(1);
            OP(2);
            OP(3);
            OP(4);
            OP(5);
            OP(6);
            OP(7);
#undef OP
        } else {
#define OP(i) v_tempBlurredAss##i = v_srcAss##i;

            OP(0);
            OP(1);
            OP(2);
            OP(3);
            OP(4);
            OP(5);
            OP(6);
            OP(7);
#undef OP
        }
    } else {
        if (d < maxNoise[0]) {
            const vector signed short vsint16_7 = vec_splat_s16(7);
            const vector signed short vsint16_4 = vec_splat_s16(4);
            const vector unsigned short vuint16_3 = vec_splat_u16(3);

#define OP(i)                                                   \
            const vector signed short v_temp##i =               \
                vec_mladd(v_tempBlurredAss##i,                  \
                          vsint16_7, v_srcAss##i);              \
            const vector signed short v_temp2##i =              \
                vec_add(v_temp##i, vsint16_4);                  \
            v_tempBlurredAss##i = vec_sr(v_temp2##i, vuint16_3)

            OP(0);
            OP(1);
            OP(2);
            OP(3);
            OP(4);
            OP(5);
            OP(6);
            OP(7);
#undef OP
        } else {
            const vector signed short vsint16_3 = vec_splat_s16(3);
            const vector signed short vsint16_2 = vec_splat_s16(2);

#define OP(i)                                                   \
            const vector signed short v_temp##i =               \
                vec_mladd(v_tempBlurredAss##i,                  \
                          vsint16_3, v_srcAss##i);              \
            const vector signed short v_temp2##i =              \
                vec_add(v_temp##i, vsint16_2);                  \
            v_tempBlurredAss##i = vec_sr(v_temp2##i, (vector unsigned short)vsint16_2)

            OP(0);
            OP(1);
            OP(2);
            OP(3);
            OP(4);
            OP(5);
            OP(6);
            OP(7);
#undef OP
        }
    }

    const vector signed char neg1 = vec_splat_s8(-1);
    const vector unsigned char permHH = (const vector unsigned char){0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                                                     0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

#define PACK_AND_STORE(src, i)                                  \
    const vector unsigned char perms##src##i =                  \
        vec_lvsr(i * stride, src);                              \
    const vector unsigned char vf##src##i =                     \
        vec_packsu(v_tempBlurredAss##i, (vector signed short)zero); \
    const vector unsigned char vg##src##i =                     \
        vec_perm(vf##src##i, v_##src##A##i, permHH);            \
    const vector unsigned char mask##src##i =                   \
        vec_perm((vector unsigned char)zero, (vector unsigned char)neg1, perms##src##i); \
    const vector unsigned char vg2##src##i =                    \
        vec_perm(vg##src##i, vg##src##i, perms##src##i);        \
    const vector unsigned char svA##src##i =                    \
        vec_sel(v_##src##A1##i, vg2##src##i, mask##src##i);     \
    const vector unsigned char svB##src##i =                    \
        vec_sel(vg2##src##i, v_##src##A2##i, mask##src##i);     \
    vec_st(svA##src##i, i * stride, src);                       \
    vec_st(svB##src##i, i * stride + 16, src)

    PACK_AND_STORE(src, 0);
    PACK_AND_STORE(src, 1);
    PACK_AND_STORE(src, 2);
    PACK_AND_STORE(src, 3);
    PACK_AND_STORE(src, 4);
    PACK_AND_STORE(src, 5);
    PACK_AND_STORE(src, 6);
    PACK_AND_STORE(src, 7);
    PACK_AND_STORE(tempBlurred, 0);
    PACK_AND_STORE(tempBlurred, 1);
    PACK_AND_STORE(tempBlurred, 2);
    PACK_AND_STORE(tempBlurred, 3);
    PACK_AND_STORE(tempBlurred, 4);
    PACK_AND_STORE(tempBlurred, 5);
    PACK_AND_STORE(tempBlurred, 6);
    PACK_AND_STORE(tempBlurred, 7);
#undef PACK_AND_STORE
}

static inline void transpose_16x8_char_toPackedAlign_altivec(unsigned char* dst, unsigned char* src, int stride) {
    const vector unsigned char zero = vec_splat_u8(0);

#define LOAD_DOUBLE_LINE(i, j)                                          \
    vector unsigned char perm1##i = vec_lvsl(i * stride, src);          \
    vector unsigned char perm2##i = vec_lvsl(j * stride, src);          \
    vector unsigned char srcA##i = vec_ld(i * stride, src);             \
    vector unsigned char srcB##i = vec_ld(i * stride + 16, src);        \
    vector unsigned char srcC##i = vec_ld(j * stride, src);             \
    vector unsigned char srcD##i = vec_ld(j * stride+ 16, src);         \
    vector unsigned char src##i = vec_perm(srcA##i, srcB##i, perm1##i); \
    vector unsigned char src##j = vec_perm(srcC##i, srcD##i, perm2##i)

    LOAD_DOUBLE_LINE(0, 1);
    LOAD_DOUBLE_LINE(2, 3);
    LOAD_DOUBLE_LINE(4, 5);
    LOAD_DOUBLE_LINE(6, 7);
#undef LOAD_DOUBLE_LINE

    vector unsigned char tempA = vec_mergeh(src0, zero);
    vector unsigned char tempB = vec_mergel(src0, zero);
    vector unsigned char tempC = vec_mergeh(src1, zero);
    vector unsigned char tempD = vec_mergel(src1, zero);
    vector unsigned char tempE = vec_mergeh(src2, zero);
    vector unsigned char tempF = vec_mergel(src2, zero);
    vector unsigned char tempG = vec_mergeh(src3, zero);
    vector unsigned char tempH = vec_mergel(src3, zero);
    vector unsigned char tempI = vec_mergeh(src4, zero);
    vector unsigned char tempJ = vec_mergel(src4, zero);
    vector unsigned char tempK = vec_mergeh(src5, zero);
    vector unsigned char tempL = vec_mergel(src5, zero);
    vector unsigned char tempM = vec_mergeh(src6, zero);
    vector unsigned char tempN = vec_mergel(src6, zero);
    vector unsigned char tempO = vec_mergeh(src7, zero);
    vector unsigned char tempP = vec_mergel(src7, zero);

    vector unsigned char temp0  = vec_mergeh(tempA, tempI);
    vector unsigned char temp1  = vec_mergel(tempA, tempI);
    vector unsigned char temp2  = vec_mergeh(tempB, tempJ);
    vector unsigned char temp3  = vec_mergel(tempB, tempJ);
    vector unsigned char temp4  = vec_mergeh(tempC, tempK);
    vector unsigned char temp5  = vec_mergel(tempC, tempK);
    vector unsigned char temp6  = vec_mergeh(tempD, tempL);
    vector unsigned char temp7  = vec_mergel(tempD, tempL);
    vector unsigned char temp8  = vec_mergeh(tempE, tempM);
    vector unsigned char temp9  = vec_mergel(tempE, tempM);
    vector unsigned char temp10 = vec_mergeh(tempF, tempN);
    vector unsigned char temp11 = vec_mergel(tempF, tempN);
    vector unsigned char temp12 = vec_mergeh(tempG, tempO);
    vector unsigned char temp13 = vec_mergel(tempG, tempO);
    vector unsigned char temp14 = vec_mergeh(tempH, tempP);
    vector unsigned char temp15 = vec_mergel(tempH, tempP);

    tempA = vec_mergeh(temp0, temp8);
    tempB = vec_mergel(temp0, temp8);
    tempC = vec_mergeh(temp1, temp9);
    tempD = vec_mergel(temp1, temp9);
    tempE = vec_mergeh(temp2, temp10);
    tempF = vec_mergel(temp2, temp10);
    tempG = vec_mergeh(temp3, temp11);
    tempH = vec_mergel(temp3, temp11);
    tempI = vec_mergeh(temp4, temp12);
    tempJ = vec_mergel(temp4, temp12);
    tempK = vec_mergeh(temp5, temp13);
    tempL = vec_mergel(temp5, temp13);
    tempM = vec_mergeh(temp6, temp14);
    tempN = vec_mergel(temp6, temp14);
    tempO = vec_mergeh(temp7, temp15);
    tempP = vec_mergel(temp7, temp15);

    temp0  = vec_mergeh(tempA, tempI);
    temp1  = vec_mergel(tempA, tempI);
    temp2  = vec_mergeh(tempB, tempJ);
    temp3  = vec_mergel(tempB, tempJ);
    temp4  = vec_mergeh(tempC, tempK);
    temp5  = vec_mergel(tempC, tempK);
    temp6  = vec_mergeh(tempD, tempL);
    temp7  = vec_mergel(tempD, tempL);
    temp8  = vec_mergeh(tempE, tempM);
    temp9  = vec_mergel(tempE, tempM);
    temp10 = vec_mergeh(tempF, tempN);
    temp11 = vec_mergel(tempF, tempN);
    temp12 = vec_mergeh(tempG, tempO);
    temp13 = vec_mergel(tempG, tempO);
    temp14 = vec_mergeh(tempH, tempP);
    temp15 = vec_mergel(tempH, tempP);

    vec_st(temp0,    0, dst);
    vec_st(temp1,   16, dst);
    vec_st(temp2,   32, dst);
    vec_st(temp3,   48, dst);
    vec_st(temp4,   64, dst);
    vec_st(temp5,   80, dst);
    vec_st(temp6,   96, dst);
    vec_st(temp7,  112, dst);
    vec_st(temp8,  128, dst);
    vec_st(temp9,  144, dst);
    vec_st(temp10, 160, dst);
    vec_st(temp11, 176, dst);
    vec_st(temp12, 192, dst);
    vec_st(temp13, 208, dst);
    vec_st(temp14, 224, dst);
    vec_st(temp15, 240, dst);
}

static inline void transpose_8x16_char_fromPackedAlign_altivec(unsigned char* dst, unsigned char* src, int stride) {
    const vector unsigned char zero = vec_splat_u8(0);

#define LOAD_DOUBLE_LINE(i, j)                                  \
    vector unsigned char src##i = vec_ld(i * 16, src);            \
    vector unsigned char src##j = vec_ld(j * 16, src)

    LOAD_DOUBLE_LINE(0, 1);
    LOAD_DOUBLE_LINE(2, 3);
    LOAD_DOUBLE_LINE(4, 5);
    LOAD_DOUBLE_LINE(6, 7);
    LOAD_DOUBLE_LINE(8, 9);
    LOAD_DOUBLE_LINE(10, 11);
    LOAD_DOUBLE_LINE(12, 13);
    LOAD_DOUBLE_LINE(14, 15);
#undef LOAD_DOUBLE_LINE

    vector unsigned char tempA = vec_mergeh(src0, src8);
    vector unsigned char tempB;
    vector unsigned char tempC = vec_mergeh(src1, src9);
    vector unsigned char tempD;
    vector unsigned char tempE = vec_mergeh(src2, src10);
    vector unsigned char tempG = vec_mergeh(src3, src11);
    vector unsigned char tempI = vec_mergeh(src4, src12);
    vector unsigned char tempJ;
    vector unsigned char tempK = vec_mergeh(src5, src13);
    vector unsigned char tempL;
    vector unsigned char tempM = vec_mergeh(src6, src14);
    vector unsigned char tempO = vec_mergeh(src7, src15);

    vector unsigned char temp0 = vec_mergeh(tempA, tempI);
    vector unsigned char temp1 = vec_mergel(tempA, tempI);
    vector unsigned char temp2;
    vector unsigned char temp3;
    vector unsigned char temp4 = vec_mergeh(tempC, tempK);
    vector unsigned char temp5 = vec_mergel(tempC, tempK);
    vector unsigned char temp6;
    vector unsigned char temp7;
    vector unsigned char temp8 = vec_mergeh(tempE, tempM);
    vector unsigned char temp9 = vec_mergel(tempE, tempM);
    vector unsigned char temp12 = vec_mergeh(tempG, tempO);
    vector unsigned char temp13 = vec_mergel(tempG, tempO);

    tempA = vec_mergeh(temp0, temp8);
    tempB = vec_mergel(temp0, temp8);
    tempC = vec_mergeh(temp1, temp9);
    tempD = vec_mergel(temp1, temp9);
    tempI = vec_mergeh(temp4, temp12);
    tempJ = vec_mergel(temp4, temp12);
    tempK = vec_mergeh(temp5, temp13);
    tempL = vec_mergel(temp5, temp13);

    temp0 = vec_mergeh(tempA, tempI);
    temp1 = vec_mergel(tempA, tempI);
    temp2 = vec_mergeh(tempB, tempJ);
    temp3 = vec_mergel(tempB, tempJ);
    temp4 = vec_mergeh(tempC, tempK);
    temp5 = vec_mergel(tempC, tempK);
    temp6 = vec_mergeh(tempD, tempL);
    temp7 = vec_mergel(tempD, tempL);


    const vector signed char neg1 = vec_splat_s8(-1);
#define STORE_DOUBLE_LINE(i, j)                                         \
    vector unsigned char dstA##i = vec_ld(i * stride, dst);             \
    vector unsigned char dstB##i = vec_ld(i * stride + 16, dst);        \
    vector unsigned char dstA##j = vec_ld(j * stride, dst);             \
    vector unsigned char dstB##j = vec_ld(j * stride+ 16, dst);         \
    vector unsigned char align##i = vec_lvsr(i * stride, dst);          \
    vector unsigned char align##j = vec_lvsr(j * stride, dst);          \
    vector unsigned char mask##i = vec_perm(zero, (vector unsigned char)neg1, align##i); \
    vector unsigned char mask##j = vec_perm(zero, (vector unsigned char)neg1, align##j); \
    vector unsigned char dstR##i = vec_perm(temp##i, temp##i, align##i);\
    vector unsigned char dstR##j = vec_perm(temp##j, temp##j, align##j);\
    vector unsigned char dstAF##i = vec_sel(dstA##i, dstR##i, mask##i); \
    vector unsigned char dstBF##i = vec_sel(dstR##i, dstB##i, mask##i); \
    vector unsigned char dstAF##j = vec_sel(dstA##j, dstR##j, mask##j); \
    vector unsigned char dstBF##j = vec_sel(dstR##j, dstB##j, mask##j); \
    vec_st(dstAF##i, i * stride, dst);                                  \
    vec_st(dstBF##i, i * stride + 16, dst);                             \
    vec_st(dstAF##j, j * stride, dst);                                  \
    vec_st(dstBF##j, j * stride + 16, dst)

    STORE_DOUBLE_LINE(0,1);
    STORE_DOUBLE_LINE(2,3);
    STORE_DOUBLE_LINE(4,5);
    STORE_DOUBLE_LINE(6,7);
}
