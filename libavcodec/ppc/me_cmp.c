/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "libavcodec/avcodec.h"
#include "libavcodec/mpegvideo.h"
#include "libavcodec/me_cmp.h"

#if HAVE_ALTIVEC

#if HAVE_BIGENDIAN
#define GET_PERM(per1, per2, pix) {\
    per1 = vec_lvsl(0, pix);\
    per2 = vec_add(per1, vec_splat_u8(1));\
}
#define LOAD_PIX(v, iv, pix, per1, per2) {\
    vector unsigned char pix2l  = vec_ld(0,  pix);\
    vector unsigned char pix2r  = vec_ld(16, pix);\
    v  = vec_perm(pix2l, pix2r, per1);\
    iv = vec_perm(pix2l, pix2r, per2);\
}
#else
#define GET_PERM(per1, per2, pix) {}
#define LOAD_PIX(v, iv, pix, per1, per2) {\
    v  = vec_vsx_ld(0,  pix);\
    iv = vec_vsx_ld(1,  pix);\
}
#endif
static int sad16_x2_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                            ptrdiff_t stride, int h)
{
    int i;
    int __attribute__((aligned(16))) s = 0;
    const vector unsigned char zero =
        (const vector unsigned char) vec_splat_u8(0);
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;
    vector unsigned char perm1, perm2, pix2v, pix2iv;

    GET_PERM(perm1, perm2, pix2);
    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
         * pix1v: pix1[0] - pix1[15]
         * pix2v: pix2[0] - pix2[15]      pix2iv: pix2[1] - pix2[16] */
        vector unsigned char pix1v  = vec_ld(0,  pix1);
        LOAD_PIX(pix2v, pix2iv, pix2, perm1, perm2);

        /* Calculate the average vector. */
        vector unsigned char avgv = vec_avg(pix2v, pix2iv);

        /* Calculate a sum of abs differences vector. */
        vector unsigned char t5 = vec_sub(vec_max(pix1v, avgv),
                                          vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t5, sad);

        pix1 += stride;
        pix2 += stride;
    }
    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad16_y2_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                            ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s = 0;
    const vector unsigned char zero =
        (const vector unsigned char) vec_splat_u8(0);
    vector unsigned char pix1v, pix3v, avgv, t5;
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;

    uint8_t *pix3 = pix2 + stride;

    /* Due to the fact that pix3 = pix2 + stride, the pix3 of one
     * iteration becomes pix2 in the next iteration. We can use this
     * fact to avoid a potentially expensive unaligned read, each
     * time around the loop.
     * Read unaligned pixels into our vectors. The vectors are as follows:
     * pix2v: pix2[0] - pix2[15]
     * Split the pixel vectors into shorts. */
    vector unsigned char pix2v = VEC_LD(0, pix2);

    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
         * pix1v: pix1[0] - pix1[15]
         * pix3v: pix3[0] - pix3[15] */
        pix1v = vec_ld(0,  pix1);
        pix3v = VEC_LD(0,  pix3);

        /* Calculate the average vector. */
        avgv = vec_avg(pix2v, pix3v);

        /* Calculate a sum of abs differences vector. */
        t5 = vec_sub(vec_max(pix1v, avgv), vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t5, sad);

        pix1 += stride;
        pix2v = pix3v;
        pix3 += stride;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);
    return s;
}

static int sad16_xy2_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                             ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s = 0;
    uint8_t *pix3 = pix2 + stride;
    const vector unsigned char zero =
        (const vector unsigned char) vec_splat_u8(0);
    const vector unsigned short two =
        (const vector unsigned short) vec_splat_u16(2);
    vector unsigned char avgv, t5;
    vector unsigned char pix1v, pix3v, pix3iv;
    vector unsigned short pix3lv, pix3hv, pix3ilv, pix3ihv;
    vector unsigned short avghv, avglv;
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;
    vector unsigned char perm1, perm2, pix2v, pix2iv;
    GET_PERM(perm1, perm2, pix2);

    /* Due to the fact that pix3 = pix2 + stride, the pix3 of one
     * iteration becomes pix2 in the next iteration. We can use this
     * fact to avoid a potentially expensive unaligned read, as well
     * as some splitting, and vector addition each time around the loop.
     * Read unaligned pixels into our vectors. The vectors are as follows:
     * pix2v: pix2[0] - pix2[15]  pix2iv: pix2[1] - pix2[16]
     * Split the pixel vectors into shorts. */
    LOAD_PIX(pix2v, pix2iv, pix2, perm1, perm2);
    vector unsigned short pix2hv  =
        (vector unsigned short) VEC_MERGEH(zero, pix2v);
    vector unsigned short pix2lv  =
        (vector unsigned short) VEC_MERGEL(zero, pix2v);
    vector unsigned short pix2ihv =
        (vector unsigned short) VEC_MERGEH(zero, pix2iv);
    vector unsigned short pix2ilv =
        (vector unsigned short) VEC_MERGEL(zero, pix2iv);

    vector unsigned short t1 = vec_add(pix2hv, pix2ihv);
    vector unsigned short t2 = vec_add(pix2lv, pix2ilv);
    vector unsigned short t3, t4;

    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
         * pix1v: pix1[0] - pix1[15]
         * pix3v: pix3[0] - pix3[15]      pix3iv: pix3[1] - pix3[16] */
        pix1v  = vec_ld(0, pix1);
        LOAD_PIX(pix3v, pix3iv, pix3, perm1, perm2);

        /* Note that AltiVec does have vec_avg, but this works on vector pairs
         * and rounds up. We could do avg(avg(a, b), avg(c, d)), but the
         * rounding would mean that, for example, avg(3, 0, 0, 1) = 2, when
         * it should be 1. Instead, we have to split the pixel vectors into
         * vectors of shorts and do the averaging by hand. */

        /* Split the pixel vectors into shorts. */
        pix3hv  = (vector unsigned short) VEC_MERGEH(zero, pix3v);
        pix3lv  = (vector unsigned short) VEC_MERGEL(zero, pix3v);
        pix3ihv = (vector unsigned short) VEC_MERGEH(zero, pix3iv);
        pix3ilv = (vector unsigned short) VEC_MERGEL(zero, pix3iv);

        /* Do the averaging on them. */
        t3 = vec_add(pix3hv, pix3ihv);
        t4 = vec_add(pix3lv, pix3ilv);

        avghv = vec_sr(vec_add(vec_add(t1, t3), two), two);
        avglv = vec_sr(vec_add(vec_add(t2, t4), two), two);

        /* Pack the shorts back into a result. */
        avgv = vec_pack(avghv, avglv);

        /* Calculate a sum of abs differences vector. */
        t5 = vec_sub(vec_max(pix1v, avgv), vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t5, sad);

        pix1 += stride;
        pix3 += stride;
        /* Transfer the calculated values for pix3 into pix2. */
        t1 = t3;
        t2 = t4;
    }
    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad16_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2. */
        vector unsigned char t1 =vec_ld(0, pix1);
        vector unsigned char t2 = VEC_LD(0, pix2);

        /* Calculate a sum of abs differences vector. */
        vector unsigned char t3 = vec_max(t1, t2);
        vector unsigned char t4 = vec_min(t1, t2);
        vector unsigned char t5 = vec_sub(t3, t4);

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t5, sad);

        pix1 += stride;
        pix2 += stride;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad8_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    const vector unsigned char permclear =
        (vector unsigned char)
        { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 };
    vector unsigned int sad = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumdiffs;

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2.
         * Since we're reading 16 pixels, and actually only want 8,
         * mask out the last 8 pixels. The 0s don't change the sum. */
        vector unsigned char pix1l = VEC_LD(0, pix1);
        vector unsigned char pix2l = VEC_LD(0, pix2);
        vector unsigned char t1 = vec_and(pix1l, permclear);
        vector unsigned char t2 = vec_and(pix2l, permclear);

        /* Calculate a sum of abs differences vector. */
        vector unsigned char t3 = vec_max(t1, t2);
        vector unsigned char t4 = vec_min(t1, t2);
        vector unsigned char t5 = vec_sub(t3, t4);

        /* Add each 4 pixel group together and put 4 results into sad. */
        sad = vec_sum4s(t5, sad);

        pix1 += stride;
        pix2 += stride;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

/* Sum of Squared Errors for an 8x8 block, AltiVec-enhanced.
 * It's the sad8_altivec code above w/ squaring added. */
static int sse8_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                        ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    const vector unsigned char permclear =
        (vector unsigned char)
        { 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0 };
    vector unsigned int sum = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumsqr;

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2.
         * Since we're reading 16 pixels, and actually only want 8,
         * mask out the last 8 pixels. The 0s don't change the sum. */
        vector unsigned char t1 = vec_and(VEC_LD(0, pix1), permclear);
        vector unsigned char t2 = vec_and(VEC_LD(0, pix2), permclear);

        /* Since we want to use unsigned chars, we can take advantage
         * of the fact that abs(a - b) ^ 2 = (a - b) ^ 2. */

        /* Calculate abs differences vector. */
        vector unsigned char t3 = vec_max(t1, t2);
        vector unsigned char t4 = vec_min(t1, t2);
        vector unsigned char t5 = vec_sub(t3, t4);

        /* Square the values and add them to our sum. */
        sum = vec_msum(t5, t5, sum);

        pix1 += stride;
        pix2 += stride;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumsqr = vec_sums((vector signed int) sum, (vector signed int) zero);
    sumsqr = vec_splat(sumsqr, 3);
    vec_ste(sumsqr, 0, &s);

    return s;
}

/* Sum of Squared Errors for a 16x16 block, AltiVec-enhanced.
 * It's the sad16_altivec code above w/ squaring added. */
static int sse16_altivec(MpegEncContext *v, uint8_t *pix1, uint8_t *pix2,
                         ptrdiff_t stride, int h)
{
    int i;
    int  __attribute__((aligned(16))) s;
    const vector unsigned int zero =
        (const vector unsigned int) vec_splat_u32(0);
    vector unsigned int sum = (vector unsigned int) vec_splat_u32(0);
    vector signed int sumsqr;

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2. */
        vector unsigned char t1 = vec_ld(0, pix1);
        vector unsigned char t2 = VEC_LD(0, pix2);

        /* Since we want to use unsigned chars, we can take advantage
         * of the fact that abs(a - b) ^ 2 = (a - b) ^ 2. */

        /* Calculate abs differences vector. */
        vector unsigned char t3 = vec_max(t1, t2);
        vector unsigned char t4 = vec_min(t1, t2);
        vector unsigned char t5 = vec_sub(t3, t4);

        /* Square the values and add them to our sum. */
        sum = vec_msum(t5, t5, sum);

        pix1 += stride;
        pix2 += stride;
    }

    /* Sum up the four partial sums, and put the result into s. */
    sumsqr = vec_sums((vector signed int) sum, (vector signed int) zero);
    sumsqr = vec_splat(sumsqr, 3);

    vec_ste(sumsqr, 0, &s);
    return s;
}

static int hadamard8_diff8x8_altivec(MpegEncContext *s, uint8_t *dst,
                                     uint8_t *src, ptrdiff_t stride, int h)
{
    int __attribute__((aligned(16))) sum;
    register const vector unsigned char vzero =
        (const vector unsigned char) vec_splat_u8(0);
    register vector signed short temp0, temp1, temp2, temp3, temp4,
                                 temp5, temp6, temp7;
    {
        register const vector signed short vprod1 =
            (const vector signed short) { 1, -1, 1, -1, 1, -1, 1, -1 };
        register const vector signed short vprod2 =
            (const vector signed short) { 1, 1, -1, -1, 1, 1, -1, -1 };
        register const vector signed short vprod3 =
            (const vector signed short) { 1, 1, 1, 1, -1, -1, -1, -1 };
        register const vector unsigned char perm1 =
            (const vector unsigned char)
            { 0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
              0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D };
        register const vector unsigned char perm2 =
            (const vector unsigned char)
            { 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03,
              0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B };
        register const vector unsigned char perm3 =
            (const vector unsigned char)
            { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };


#define ONEITERBUTTERFLY(i, res)                                            \
    {                                                                       \
        register vector unsigned char srcO =  unaligned_load(stride * i, src);  \
        register vector unsigned char dstO = unaligned_load(stride * i, dst);\
                                                                            \
        /* Promote the unsigned chars to signed shorts. */                  \
        /* We're in the 8x8 function, we only care for the first 8. */      \
        register vector signed short srcV =                                 \
            (vector signed short) VEC_MERGEH((vector signed char) vzero,    \
                                             (vector signed char) srcO);    \
        register vector signed short dstV =                                 \
            (vector signed short) VEC_MERGEH((vector signed char) vzero,    \
                                             (vector signed char) dstO);    \
                                                                            \
        /* subtractions inside the first butterfly */                       \
        register vector signed short but0 = vec_sub(srcV, dstV);            \
        register vector signed short op1  = vec_perm(but0, but0, perm1);    \
        register vector signed short but1 = vec_mladd(but0, vprod1, op1);   \
        register vector signed short op2  = vec_perm(but1, but1, perm2);    \
        register vector signed short but2 = vec_mladd(but1, vprod2, op2);   \
        register vector signed short op3  = vec_perm(but2, but2, perm3);    \
        res  = vec_mladd(but2, vprod3, op3);                                \
    }

        ONEITERBUTTERFLY(0, temp0);
        ONEITERBUTTERFLY(1, temp1);
        ONEITERBUTTERFLY(2, temp2);
        ONEITERBUTTERFLY(3, temp3);
        ONEITERBUTTERFLY(4, temp4);
        ONEITERBUTTERFLY(5, temp5);
        ONEITERBUTTERFLY(6, temp6);
        ONEITERBUTTERFLY(7, temp7);
    }
#undef ONEITERBUTTERFLY
    {
        register vector signed int vsum;
        register vector signed short line0  = vec_add(temp0, temp1);
        register vector signed short line1  = vec_sub(temp0, temp1);
        register vector signed short line2  = vec_add(temp2, temp3);
        register vector signed short line3  = vec_sub(temp2, temp3);
        register vector signed short line4  = vec_add(temp4, temp5);
        register vector signed short line5  = vec_sub(temp4, temp5);
        register vector signed short line6  = vec_add(temp6, temp7);
        register vector signed short line7  = vec_sub(temp6, temp7);

        register vector signed short line0B = vec_add(line0, line2);
        register vector signed short line2B = vec_sub(line0, line2);
        register vector signed short line1B = vec_add(line1, line3);
        register vector signed short line3B = vec_sub(line1, line3);
        register vector signed short line4B = vec_add(line4, line6);
        register vector signed short line6B = vec_sub(line4, line6);
        register vector signed short line5B = vec_add(line5, line7);
        register vector signed short line7B = vec_sub(line5, line7);

        register vector signed short line0C = vec_add(line0B, line4B);
        register vector signed short line4C = vec_sub(line0B, line4B);
        register vector signed short line1C = vec_add(line1B, line5B);
        register vector signed short line5C = vec_sub(line1B, line5B);
        register vector signed short line2C = vec_add(line2B, line6B);
        register vector signed short line6C = vec_sub(line2B, line6B);
        register vector signed short line3C = vec_add(line3B, line7B);
        register vector signed short line7C = vec_sub(line3B, line7B);

        vsum = vec_sum4s(vec_abs(line0C), vec_splat_s32(0));
        vsum = vec_sum4s(vec_abs(line1C), vsum);
        vsum = vec_sum4s(vec_abs(line2C), vsum);
        vsum = vec_sum4s(vec_abs(line3C), vsum);
        vsum = vec_sum4s(vec_abs(line4C), vsum);
        vsum = vec_sum4s(vec_abs(line5C), vsum);
        vsum = vec_sum4s(vec_abs(line6C), vsum);
        vsum = vec_sum4s(vec_abs(line7C), vsum);
        vsum = vec_sums(vsum, (vector signed int) vzero);
        vsum = vec_splat(vsum, 3);

        vec_ste(vsum, 0, &sum);
    }
    return sum;
}

/*
 * 16x8 works with 16 elements; it can avoid replicating loads, and
 * gives the compiler more room for scheduling. It's only used from
 * inside hadamard8_diff16_altivec.
 *
 * Unfortunately, it seems gcc-3.3 is a bit dumb, and the compiled code has
 * a LOT of spill code, it seems gcc (unlike xlc) cannot keep everything in
 * registers by itself. The following code includes hand-made register
 * allocation. It's not clean, but on a 7450 the resulting code is much faster
 * (best case falls from 700+ cycles to 550).
 *
 * xlc doesn't add spill code, but it doesn't know how to schedule for the
 * 7450, and its code isn't much faster than gcc-3.3 on the 7450 (but uses
 * 25% fewer instructions...)
 *
 * On the 970, the hand-made RA is still a win (around 690 vs. around 780),
 * but xlc goes to around 660 on the regular C code...
 */
static int hadamard8_diff16x8_altivec(MpegEncContext *s, uint8_t *dst,
                                      uint8_t *src, ptrdiff_t stride, int h)
{
    int __attribute__((aligned(16))) sum;
    register vector signed short
        temp0 __asm__ ("v0"),
        temp1 __asm__ ("v1"),
        temp2 __asm__ ("v2"),
        temp3 __asm__ ("v3"),
        temp4 __asm__ ("v4"),
        temp5 __asm__ ("v5"),
        temp6 __asm__ ("v6"),
        temp7 __asm__ ("v7");
    register vector signed short
        temp0S __asm__ ("v8"),
        temp1S __asm__ ("v9"),
        temp2S __asm__ ("v10"),
        temp3S __asm__ ("v11"),
        temp4S __asm__ ("v12"),
        temp5S __asm__ ("v13"),
        temp6S __asm__ ("v14"),
        temp7S __asm__ ("v15");
    register const vector unsigned char vzero __asm__ ("v31") =
        (const vector unsigned char) vec_splat_u8(0);
    {
        register const vector signed short vprod1 __asm__ ("v16") =
            (const vector signed short) { 1, -1, 1, -1, 1, -1, 1, -1 };

        register const vector signed short vprod2 __asm__ ("v17") =
            (const vector signed short) { 1, 1, -1, -1, 1, 1, -1, -1 };

        register const vector signed short vprod3 __asm__ ("v18") =
            (const vector signed short) { 1, 1, 1, 1, -1, -1, -1, -1 };

        register const vector unsigned char perm1 __asm__ ("v19") =
            (const vector unsigned char)
            { 0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
              0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D };

        register const vector unsigned char perm2 __asm__ ("v20") =
            (const vector unsigned char)
            { 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03,
              0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B };

        register const vector unsigned char perm3 __asm__ ("v21") =
            (const vector unsigned char)
            { 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
              0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

#define ONEITERBUTTERFLY(i, res1, res2)                                     \
    {                                                                       \
        register vector unsigned char srcO __asm__ ("v22") =                \
            unaligned_load(stride * i, src);                                    \
        register vector unsigned char dstO __asm__ ("v23") =                \
            unaligned_load(stride * i, dst);\
                                                                            \
        /* Promote the unsigned chars to signed shorts. */                  \
        register vector signed short srcV __asm__ ("v24") =                 \
            (vector signed short) VEC_MERGEH((vector signed char) vzero,    \
                                             (vector signed char) srcO);    \
        register vector signed short dstV __asm__ ("v25") =                 \
            (vector signed short) VEC_MERGEH((vector signed char) vzero,    \
                                             (vector signed char) dstO);    \
        register vector signed short srcW __asm__ ("v26") =                 \
            (vector signed short) VEC_MERGEL((vector signed char) vzero,    \
                                             (vector signed char) srcO);    \
        register vector signed short dstW __asm__ ("v27") =                 \
            (vector signed short) VEC_MERGEL((vector signed char) vzero,    \
                                             (vector signed char) dstO);    \
                                                                            \
        /* subtractions inside the first butterfly */                       \
        register vector signed short but0  __asm__ ("v28") =                \
            vec_sub(srcV, dstV);                                            \
        register vector signed short but0S __asm__ ("v29") =                \
            vec_sub(srcW, dstW);                                            \
        register vector signed short op1   __asm__ ("v30") =                \
            vec_perm(but0, but0, perm1);                                    \
        register vector signed short but1  __asm__ ("v22") =                \
            vec_mladd(but0, vprod1, op1);                                   \
        register vector signed short op1S  __asm__ ("v23") =                \
            vec_perm(but0S, but0S, perm1);                                  \
        register vector signed short but1S __asm__ ("v24") =                \
            vec_mladd(but0S, vprod1, op1S);                                 \
        register vector signed short op2   __asm__ ("v25") =                \
            vec_perm(but1, but1, perm2);                                    \
        register vector signed short but2  __asm__ ("v26") =                \
            vec_mladd(but1, vprod2, op2);                                   \
        register vector signed short op2S  __asm__ ("v27") =                \
            vec_perm(but1S, but1S, perm2);                                  \
        register vector signed short but2S __asm__ ("v28") =                \
            vec_mladd(but1S, vprod2, op2S);                                 \
        register vector signed short op3   __asm__ ("v29") =                \
            vec_perm(but2, but2, perm3);                                    \
        register vector signed short op3S  __asm__ ("v30") =                \
            vec_perm(but2S, but2S, perm3);                                  \
        res1 = vec_mladd(but2, vprod3, op3);                                \
        res2 = vec_mladd(but2S, vprod3, op3S);                              \
    }

        ONEITERBUTTERFLY(0, temp0, temp0S);
        ONEITERBUTTERFLY(1, temp1, temp1S);
        ONEITERBUTTERFLY(2, temp2, temp2S);
        ONEITERBUTTERFLY(3, temp3, temp3S);
        ONEITERBUTTERFLY(4, temp4, temp4S);
        ONEITERBUTTERFLY(5, temp5, temp5S);
        ONEITERBUTTERFLY(6, temp6, temp6S);
        ONEITERBUTTERFLY(7, temp7, temp7S);
    }
#undef ONEITERBUTTERFLY
    {
        register vector signed int vsum;

        register vector signed short line0  = vec_add(temp0, temp1);
        register vector signed short line1  = vec_sub(temp0, temp1);
        register vector signed short line2  = vec_add(temp2, temp3);
        register vector signed short line3  = vec_sub(temp2, temp3);
        register vector signed short line4  = vec_add(temp4, temp5);
        register vector signed short line5  = vec_sub(temp4, temp5);
        register vector signed short line6  = vec_add(temp6, temp7);
        register vector signed short line7  = vec_sub(temp6, temp7);

        register vector signed short line0B = vec_add(line0, line2);
        register vector signed short line2B = vec_sub(line0, line2);
        register vector signed short line1B = vec_add(line1, line3);
        register vector signed short line3B = vec_sub(line1, line3);
        register vector signed short line4B = vec_add(line4, line6);
        register vector signed short line6B = vec_sub(line4, line6);
        register vector signed short line5B = vec_add(line5, line7);
        register vector signed short line7B = vec_sub(line5, line7);

        register vector signed short line0C = vec_add(line0B, line4B);
        register vector signed short line4C = vec_sub(line0B, line4B);
        register vector signed short line1C = vec_add(line1B, line5B);
        register vector signed short line5C = vec_sub(line1B, line5B);
        register vector signed short line2C = vec_add(line2B, line6B);
        register vector signed short line6C = vec_sub(line2B, line6B);
        register vector signed short line3C = vec_add(line3B, line7B);
        register vector signed short line7C = vec_sub(line3B, line7B);

        register vector signed short line0S = vec_add(temp0S, temp1S);
        register vector signed short line1S = vec_sub(temp0S, temp1S);
        register vector signed short line2S = vec_add(temp2S, temp3S);
        register vector signed short line3S = vec_sub(temp2S, temp3S);
        register vector signed short line4S = vec_add(temp4S, temp5S);
        register vector signed short line5S = vec_sub(temp4S, temp5S);
        register vector signed short line6S = vec_add(temp6S, temp7S);
        register vector signed short line7S = vec_sub(temp6S, temp7S);

        register vector signed short line0BS = vec_add(line0S, line2S);
        register vector signed short line2BS = vec_sub(line0S, line2S);
        register vector signed short line1BS = vec_add(line1S, line3S);
        register vector signed short line3BS = vec_sub(line1S, line3S);
        register vector signed short line4BS = vec_add(line4S, line6S);
        register vector signed short line6BS = vec_sub(line4S, line6S);
        register vector signed short line5BS = vec_add(line5S, line7S);
        register vector signed short line7BS = vec_sub(line5S, line7S);

        register vector signed short line0CS = vec_add(line0BS, line4BS);
        register vector signed short line4CS = vec_sub(line0BS, line4BS);
        register vector signed short line1CS = vec_add(line1BS, line5BS);
        register vector signed short line5CS = vec_sub(line1BS, line5BS);
        register vector signed short line2CS = vec_add(line2BS, line6BS);
        register vector signed short line6CS = vec_sub(line2BS, line6BS);
        register vector signed short line3CS = vec_add(line3BS, line7BS);
        register vector signed short line7CS = vec_sub(line3BS, line7BS);

        vsum = vec_sum4s(vec_abs(line0C), vec_splat_s32(0));
        vsum = vec_sum4s(vec_abs(line1C), vsum);
        vsum = vec_sum4s(vec_abs(line2C), vsum);
        vsum = vec_sum4s(vec_abs(line3C), vsum);
        vsum = vec_sum4s(vec_abs(line4C), vsum);
        vsum = vec_sum4s(vec_abs(line5C), vsum);
        vsum = vec_sum4s(vec_abs(line6C), vsum);
        vsum = vec_sum4s(vec_abs(line7C), vsum);

        vsum = vec_sum4s(vec_abs(line0CS), vsum);
        vsum = vec_sum4s(vec_abs(line1CS), vsum);
        vsum = vec_sum4s(vec_abs(line2CS), vsum);
        vsum = vec_sum4s(vec_abs(line3CS), vsum);
        vsum = vec_sum4s(vec_abs(line4CS), vsum);
        vsum = vec_sum4s(vec_abs(line5CS), vsum);
        vsum = vec_sum4s(vec_abs(line6CS), vsum);
        vsum = vec_sum4s(vec_abs(line7CS), vsum);
        vsum = vec_sums(vsum, (vector signed int) vzero);
        vsum = vec_splat(vsum, 3);

        vec_ste(vsum, 0, &sum);
    }
    return sum;
}

static int hadamard8_diff16_altivec(MpegEncContext *s, uint8_t *dst,
                                    uint8_t *src, ptrdiff_t stride, int h)
{
    int score = hadamard8_diff16x8_altivec(s, dst, src, stride, 8);

    if (h == 16) {
        dst   += 8 * stride;
        src   += 8 * stride;
        score += hadamard8_diff16x8_altivec(s, dst, src, stride, 8);
    }
    return score;
}
#endif /* HAVE_ALTIVEC */

av_cold void ff_me_cmp_init_ppc(MECmpContext *c, AVCodecContext *avctx)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->pix_abs[0][1] = sad16_x2_altivec;
    c->pix_abs[0][2] = sad16_y2_altivec;
    c->pix_abs[0][3] = sad16_xy2_altivec;
    c->pix_abs[0][0] = sad16_altivec;
    c->pix_abs[1][0] = sad8_altivec;

    c->sad[0] = sad16_altivec;
    c->sad[1] = sad8_altivec;
    c->sse[0] = sse16_altivec;
    c->sse[1] = sse8_altivec;

    c->hadamard8_diff[0] = hadamard8_diff16_altivec;
    c->hadamard8_diff[1] = hadamard8_diff8x8_altivec;
#endif /* HAVE_ALTIVEC */
}
