/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif
#include "libavcodec/dsputil.h"
#include "util_altivec.h"
#include "types_altivec.h"
#include "dsputil_altivec.h"

static int sad16_x2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned char zero = (const vector unsigned char)vec_splat_u8(0);
    vector unsigned char perm1 = vec_lvsl(0, pix2);
    vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(1));
    vector unsigned char pix2l, pix2r;
    vector unsigned char pix1v, pix2v, pix2iv, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    s = 0;
    sad = (vector unsigned int)vec_splat_u32(0);
    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix2v: pix2[0]-pix2[15]      pix2iv: pix2[1]-pix2[16] */
        pix1v  = vec_ld( 0, pix1);
        pix2l  = vec_ld( 0, pix2);
        pix2r  = vec_ld(16, pix2);
        pix2v  = vec_perm(pix2l, pix2r, perm1);
        pix2iv = vec_perm(pix2l, pix2r, perm2);

        /* Calculate the average vector */
        avgv = vec_avg(pix2v, pix2iv);

        /* Calculate a sum of abs differences vector */
        t5 = vec_sub(vec_max(pix1v, avgv), vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t5, sad);

        pix1 += line_size;
        pix2 += line_size;
    }
    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad16_y2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned char zero = (const vector unsigned char)vec_splat_u8(0);
    vector unsigned char perm = vec_lvsl(0, pix2);
    vector unsigned char pix2l, pix2r;
    vector unsigned char pix1v, pix2v, pix3v, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;
    uint8_t *pix3 = pix2 + line_size;

    s = 0;
    sad = (vector unsigned int)vec_splat_u32(0);

    /* Due to the fact that pix3 = pix2 + line_size, the pix3 of one
       iteration becomes pix2 in the next iteration. We can use this
       fact to avoid a potentially expensive unaligned read, each
       time around the loop.
       Read unaligned pixels into our vectors. The vectors are as follows:
       pix2v: pix2[0]-pix2[15]
       Split the pixel vectors into shorts */
    pix2l = vec_ld( 0, pix2);
    pix2r = vec_ld(15, pix2);
    pix2v = vec_perm(pix2l, pix2r, perm);

    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix3v: pix3[0]-pix3[15] */
        pix1v = vec_ld(0, pix1);

        pix2l = vec_ld( 0, pix3);
        pix2r = vec_ld(15, pix3);
        pix3v = vec_perm(pix2l, pix2r, perm);

        /* Calculate the average vector */
        avgv = vec_avg(pix2v, pix3v);

        /* Calculate a sum of abs differences vector */
        t5 = vec_sub(vec_max(pix1v, avgv), vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t5, sad);

        pix1 += line_size;
        pix2v = pix3v;
        pix3 += line_size;

    }

    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);
    return s;
}

static int sad16_xy2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    uint8_t *pix3 = pix2 + line_size;
    const vector unsigned char zero = (const vector unsigned char)vec_splat_u8(0);
    const vector unsigned short two = (const vector unsigned short)vec_splat_u16(2);
    vector unsigned char avgv, t5;
    vector unsigned char perm1 = vec_lvsl(0, pix2);
    vector unsigned char perm2 = vec_add(perm1, vec_splat_u8(1));
    vector unsigned char pix2l, pix2r;
    vector unsigned char pix1v, pix2v, pix3v, pix2iv, pix3iv;
    vector unsigned short pix2lv, pix2hv, pix2ilv, pix2ihv;
    vector unsigned short pix3lv, pix3hv, pix3ilv, pix3ihv;
    vector unsigned short avghv, avglv;
    vector unsigned short t1, t2, t3, t4;
    vector unsigned int sad;
    vector signed int sumdiffs;

    sad = (vector unsigned int)vec_splat_u32(0);

    s = 0;

    /* Due to the fact that pix3 = pix2 + line_size, the pix3 of one
       iteration becomes pix2 in the next iteration. We can use this
       fact to avoid a potentially expensive unaligned read, as well
       as some splitting, and vector addition each time around the loop.
       Read unaligned pixels into our vectors. The vectors are as follows:
       pix2v: pix2[0]-pix2[15]  pix2iv: pix2[1]-pix2[16]
       Split the pixel vectors into shorts */
    pix2l  = vec_ld( 0, pix2);
    pix2r  = vec_ld(16, pix2);
    pix2v  = vec_perm(pix2l, pix2r, perm1);
    pix2iv = vec_perm(pix2l, pix2r, perm2);

    pix2hv  = (vector unsigned short) vec_mergeh(zero, pix2v);
    pix2lv  = (vector unsigned short) vec_mergel(zero, pix2v);
    pix2ihv = (vector unsigned short) vec_mergeh(zero, pix2iv);
    pix2ilv = (vector unsigned short) vec_mergel(zero, pix2iv);
    t1 = vec_add(pix2hv, pix2ihv);
    t2 = vec_add(pix2lv, pix2ilv);

    for (i = 0; i < h; i++) {
        /* Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix3v: pix3[0]-pix3[15]      pix3iv: pix3[1]-pix3[16] */
        pix1v = vec_ld(0, pix1);

        pix2l  = vec_ld( 0, pix3);
        pix2r  = vec_ld(16, pix3);
        pix3v  = vec_perm(pix2l, pix2r, perm1);
        pix3iv = vec_perm(pix2l, pix2r, perm2);

        /* Note that AltiVec does have vec_avg, but this works on vector pairs
           and rounds up. We could do avg(avg(a,b),avg(c,d)), but the rounding
           would mean that, for example, avg(3,0,0,1) = 2, when it should be 1.
           Instead, we have to split the pixel vectors into vectors of shorts,
           and do the averaging by hand. */

        /* Split the pixel vectors into shorts */
        pix3hv  = (vector unsigned short) vec_mergeh(zero, pix3v);
        pix3lv  = (vector unsigned short) vec_mergel(zero, pix3v);
        pix3ihv = (vector unsigned short) vec_mergeh(zero, pix3iv);
        pix3ilv = (vector unsigned short) vec_mergel(zero, pix3iv);

        /* Do the averaging on them */
        t3 = vec_add(pix3hv, pix3ihv);
        t4 = vec_add(pix3lv, pix3ilv);

        avghv = vec_sr(vec_add(vec_add(t1, t3), two), two);
        avglv = vec_sr(vec_add(vec_add(t2, t4), two), two);

        /* Pack the shorts back into a result */
        avgv = vec_pack(avghv, avglv);

        /* Calculate a sum of abs differences vector */
        t5 = vec_sub(vec_max(pix1v, avgv), vec_min(pix1v, avgv));

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t5, sad);

        pix1 += line_size;
        pix3 += line_size;
        /* Transfer the calculated values for pix3 into pix2 */
        t1 = t3;
        t2 = t4;
    }
    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix2);
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    sad = (vector unsigned int)vec_splat_u32(0);


    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2 */
        vector unsigned char pix2l = vec_ld( 0, pix2);
        vector unsigned char pix2r = vec_ld(15, pix2);
        t1 = vec_ld(0, pix1);
        t2 = vec_perm(pix2l, pix2r, perm);

        /* Calculate a sum of abs differences vector */
        t3 = vec_max(t1, t2);
        t4 = vec_min(t1, t2);
        t5 = vec_sub(t3, t4);

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t5, sad);

        pix1 += line_size;
        pix2 += line_size;
    }

    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int sad8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    const vector unsigned char permclear = (vector unsigned char){255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0};
    vector unsigned char perm1 = vec_lvsl(0, pix1);
    vector unsigned char perm2 = vec_lvsl(0, pix2);
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    sad = (vector unsigned int)vec_splat_u32(0);

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2
           Since we're reading 16 pixels, and actually only want 8,
           mask out the last 8 pixels. The 0s don't change the sum. */
        vector unsigned char pix1l = vec_ld( 0, pix1);
        vector unsigned char pix1r = vec_ld(15, pix1);
        vector unsigned char pix2l = vec_ld( 0, pix2);
        vector unsigned char pix2r = vec_ld(15, pix2);
        t1 = vec_and(vec_perm(pix1l, pix1r, perm1), permclear);
        t2 = vec_and(vec_perm(pix2l, pix2r, perm2), permclear);

        /* Calculate a sum of abs differences vector */
        t3 = vec_max(t1, t2);
        t4 = vec_min(t1, t2);
        t5 = vec_sub(t3, t4);

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t5, sad);

        pix1 += line_size;
        pix2 += line_size;
    }

    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static int pix_norm1_altivec(uint8_t *pix, int line_size)
{
    int i;
    int s;
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix);
    vector unsigned char pixv;
    vector unsigned int sv;
    vector signed int sum;

    sv = (vector unsigned int)vec_splat_u32(0);

    s = 0;
    for (i = 0; i < 16; i++) {
        /* Read in the potentially unaligned pixels */
        vector unsigned char pixl = vec_ld( 0, pix);
        vector unsigned char pixr = vec_ld(15, pix);
        pixv = vec_perm(pixl, pixr, perm);

        /* Square the values, and add them to our sum */
        sv = vec_msum(pixv, pixv, sv);

        pix += line_size;
    }
    /* Sum up the four partial sums, and put the result into s */
    sum = vec_sums((vector signed int) sv, (vector signed int) zero);
    sum = vec_splat(sum, 3);
    vec_ste(sum, 0, &s);

    return s;
}

/**
 * Sum of Squared Errors for a 8x8 block.
 * AltiVec-enhanced.
 * It's the sad8_altivec code above w/ squaring added.
 */
static int sse8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    const vector unsigned char permclear = (vector unsigned char){255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0};
    vector unsigned char perm1 = vec_lvsl(0, pix1);
    vector unsigned char perm2 = vec_lvsl(0, pix2);
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sum;
    vector signed int sumsqr;

    sum = (vector unsigned int)vec_splat_u32(0);

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2
           Since we're reading 16 pixels, and actually only want 8,
           mask out the last 8 pixels. The 0s don't change the sum. */
        vector unsigned char pix1l = vec_ld( 0, pix1);
        vector unsigned char pix1r = vec_ld(15, pix1);
        vector unsigned char pix2l = vec_ld( 0, pix2);
        vector unsigned char pix2r = vec_ld(15, pix2);
        t1 = vec_and(vec_perm(pix1l, pix1r, perm1), permclear);
        t2 = vec_and(vec_perm(pix2l, pix2r, perm2), permclear);

        /* Since we want to use unsigned chars, we can take advantage
           of the fact that abs(a-b)^2 = (a-b)^2. */

        /* Calculate abs differences vector */
        t3 = vec_max(t1, t2);
        t4 = vec_min(t1, t2);
        t5 = vec_sub(t3, t4);

        /* Square the values and add them to our sum */
        sum = vec_msum(t5, t5, sum);

        pix1 += line_size;
        pix2 += line_size;
    }

    /* Sum up the four partial sums, and put the result into s */
    sumsqr = vec_sums((vector signed int) sum, (vector signed int) zero);
    sumsqr = vec_splat(sumsqr, 3);
    vec_ste(sumsqr, 0, &s);

    return s;
}

/**
 * Sum of Squared Errors for a 16x16 block.
 * AltiVec-enhanced.
 * It's the sad16_altivec code above w/ squaring added.
 */
static int sse16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s;
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix2);
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sum;
    vector signed int sumsqr;

    sum = (vector unsigned int)vec_splat_u32(0);

    for (i = 0; i < h; i++) {
        /* Read potentially unaligned pixels into t1 and t2 */
        vector unsigned char pix2l = vec_ld( 0, pix2);
        vector unsigned char pix2r = vec_ld(15, pix2);
        t1 = vec_ld(0, pix1);
        t2 = vec_perm(pix2l, pix2r, perm);

        /* Since we want to use unsigned chars, we can take advantage
           of the fact that abs(a-b)^2 = (a-b)^2. */

        /* Calculate abs differences vector */
        t3 = vec_max(t1, t2);
        t4 = vec_min(t1, t2);
        t5 = vec_sub(t3, t4);

        /* Square the values and add them to our sum */
        sum = vec_msum(t5, t5, sum);

        pix1 += line_size;
        pix2 += line_size;
    }

    /* Sum up the four partial sums, and put the result into s */
    sumsqr = vec_sums((vector signed int) sum, (vector signed int) zero);
    sumsqr = vec_splat(sumsqr, 3);
    vec_ste(sumsqr, 0, &s);

    return s;
}

static int pix_sum_altivec(uint8_t * pix, int line_size)
{
    const vector unsigned int zero = (const vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm = vec_lvsl(0, pix);
    vector unsigned char t1;
    vector unsigned int sad;
    vector signed int sumdiffs;

    int i;
    int s;

    sad = (vector unsigned int)vec_splat_u32(0);

    for (i = 0; i < 16; i++) {
        /* Read the potentially unaligned 16 pixels into t1 */
        vector unsigned char pixl = vec_ld( 0, pix);
        vector unsigned char pixr = vec_ld(15, pix);
        t1 = vec_perm(pixl, pixr, perm);

        /* Add each 4 pixel group together and put 4 results into sad */
        sad = vec_sum4s(t1, sad);

        pix += line_size;
    }

    /* Sum up the four partial sums, and put the result into s */
    sumdiffs = vec_sums((vector signed int) sad, (vector signed int) zero);
    sumdiffs = vec_splat(sumdiffs, 3);
    vec_ste(sumdiffs, 0, &s);

    return s;
}

static void get_pixels_altivec(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
{
    int i;
    vector unsigned char perm = vec_lvsl(0, pixels);
    vector unsigned char bytes;
    const vector unsigned char zero = (const vector unsigned char)vec_splat_u8(0);
    vector signed short shorts;

    for (i = 0; i < 8; i++) {
        // Read potentially unaligned pixels.
        // We're reading 16 pixels, and actually only want 8,
        // but we simply ignore the extras.
        vector unsigned char pixl = vec_ld( 0, pixels);
        vector unsigned char pixr = vec_ld(15, pixels);
        bytes = vec_perm(pixl, pixr, perm);

        // convert the bytes into shorts
        shorts = (vector signed short)vec_mergeh(zero, bytes);

        // save the data to the block, we assume the block is 16-byte aligned
        vec_st(shorts, i*16, (vector signed short*)block);

        pixels += line_size;
    }
}

static void diff_pixels_altivec(DCTELEM *restrict block, const uint8_t *s1,
        const uint8_t *s2, int stride)
{
    int i;
    vector unsigned char perm1 = vec_lvsl(0, s1);
    vector unsigned char perm2 = vec_lvsl(0, s2);
    vector unsigned char bytes, pixl, pixr;
    const vector unsigned char zero = (const vector unsigned char)vec_splat_u8(0);
    vector signed short shorts1, shorts2;

    for (i = 0; i < 4; i++) {
        // Read potentially unaligned pixels
        // We're reading 16 pixels, and actually only want 8,
        // but we simply ignore the extras.
        pixl = vec_ld( 0, s1);
        pixr = vec_ld(15, s1);
        bytes = vec_perm(pixl, pixr, perm1);

        // convert the bytes into shorts
        shorts1 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the same for the second block of pixels
        pixl = vec_ld( 0, s2);
        pixr = vec_ld(15, s2);
        bytes = vec_perm(pixl, pixr, perm2);

        // convert the bytes into shorts
        shorts2 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the subtraction
        shorts1 = vec_sub(shorts1, shorts2);

        // save the data to the block, we assume the block is 16-byte aligned
        vec_st(shorts1, 0, (vector signed short*)block);

        s1 += stride;
        s2 += stride;
        block += 8;


        // The code below is a copy of the code above... This is a manual
        // unroll.

        // Read potentially unaligned pixels
        // We're reading 16 pixels, and actually only want 8,
        // but we simply ignore the extras.
        pixl = vec_ld( 0, s1);
        pixr = vec_ld(15, s1);
        bytes = vec_perm(pixl, pixr, perm1);

        // convert the bytes into shorts
        shorts1 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the same for the second block of pixels
        pixl = vec_ld( 0, s2);
        pixr = vec_ld(15, s2);
        bytes = vec_perm(pixl, pixr, perm2);

        // convert the bytes into shorts
        shorts2 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the subtraction
        shorts1 = vec_sub(shorts1, shorts2);

        // save the data to the block, we assume the block is 16-byte aligned
        vec_st(shorts1, 0, (vector signed short*)block);

        s1 += stride;
        s2 += stride;
        block += 8;
    }
}


static void clear_block_altivec(DCTELEM *block) {
    LOAD_ZERO;
    vec_st(zero_s16v,   0, block);
    vec_st(zero_s16v,  16, block);
    vec_st(zero_s16v,  32, block);
    vec_st(zero_s16v,  48, block);
    vec_st(zero_s16v,  64, block);
    vec_st(zero_s16v,  80, block);
    vec_st(zero_s16v,  96, block);
    vec_st(zero_s16v, 112, block);
}


static void add_bytes_altivec(uint8_t *dst, uint8_t *src, int w) {
    register int i;
    register vector unsigned char vdst, vsrc;

    /* dst and src are 16 bytes-aligned (guaranteed) */
    for (i = 0 ; (i + 15) < w ; i+=16) {
        vdst = vec_ld(i, (unsigned char*)dst);
        vsrc = vec_ld(i, (unsigned char*)src);
        vdst = vec_add(vsrc, vdst);
        vec_st(vdst, i, (unsigned char*)dst);
    }
    /* if w is not a multiple of 16 */
    for (; (i < w) ; i++) {
        dst[i] = src[i];
    }
}

/* next one assumes that ((line_size % 16) == 0) */
void ff_put_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    register vector unsigned char pixelsv1, pixelsv2;
    register vector unsigned char pixelsv1B, pixelsv2B;
    register vector unsigned char pixelsv1C, pixelsv2C;
    register vector unsigned char pixelsv1D, pixelsv2D;

    register vector unsigned char perm = vec_lvsl(0, pixels);
    int i;
    register int line_size_2 = line_size << 1;
    register int line_size_3 = line_size + line_size_2;
    register int line_size_4 = line_size << 2;

// hand-unrolling the loop by 4 gains about 15%
// mininum execution time goes from 74 to 60 cycles
// it's faster than -funroll-loops, but using
// -funroll-loops w/ this is bad - 74 cycles again.
// all this is on a 7450, tuning for the 7450
    for (i = 0; i < h; i += 4) {
        pixelsv1  = vec_ld( 0, pixels);
        pixelsv2  = vec_ld(15, pixels);
        pixelsv1B = vec_ld(line_size, pixels);
        pixelsv2B = vec_ld(15 + line_size, pixels);
        pixelsv1C = vec_ld(line_size_2, pixels);
        pixelsv2C = vec_ld(15 + line_size_2, pixels);
        pixelsv1D = vec_ld(line_size_3, pixels);
        pixelsv2D = vec_ld(15 + line_size_3, pixels);
        vec_st(vec_perm(pixelsv1, pixelsv2, perm),
               0, (unsigned char*)block);
        vec_st(vec_perm(pixelsv1B, pixelsv2B, perm),
               line_size, (unsigned char*)block);
        vec_st(vec_perm(pixelsv1C, pixelsv2C, perm),
               line_size_2, (unsigned char*)block);
        vec_st(vec_perm(pixelsv1D, pixelsv2D, perm),
               line_size_3, (unsigned char*)block);
        pixels+=line_size_4;
        block +=line_size_4;
    }
}

/* next one assumes that ((line_size % 16) == 0) */
#define op_avg(a,b)  a = ( ((a)|(b)) - ((((a)^(b))&0xFEFEFEFEUL)>>1) )
void ff_avg_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    register vector unsigned char pixelsv1, pixelsv2, pixelsv, blockv;
    register vector unsigned char perm = vec_lvsl(0, pixels);
    int i;

    for (i = 0; i < h; i++) {
        pixelsv1 = vec_ld( 0, pixels);
        pixelsv2 = vec_ld(16,pixels);
        blockv = vec_ld(0, block);
        pixelsv = vec_perm(pixelsv1, pixelsv2, perm);
        blockv = vec_avg(blockv,pixelsv);
        vec_st(blockv, 0, (unsigned char*)block);
        pixels+=line_size;
        block +=line_size;
    }
}

/* next one assumes that ((line_size % 8) == 0) */
static void avg_pixels8_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
    register vector unsigned char pixelsv1, pixelsv2, pixelsv, blockv;
    int i;

   for (i = 0; i < h; i++) {
       /* block is 8 bytes-aligned, so we're either in the
          left block (16 bytes-aligned) or in the right block (not) */
       int rightside = ((unsigned long)block & 0x0000000F);

       blockv = vec_ld(0, block);
       pixelsv1 = vec_ld( 0, pixels);
       pixelsv2 = vec_ld(16, pixels);
       pixelsv = vec_perm(pixelsv1, pixelsv2, vec_lvsl(0, pixels));

       if (rightside) {
           pixelsv = vec_perm(blockv, pixelsv, vcprm(0,1,s0,s1));
       } else {
           pixelsv = vec_perm(blockv, pixelsv, vcprm(s0,s1,2,3));
       }

       blockv = vec_avg(blockv, pixelsv);

       vec_st(blockv, 0, block);

       pixels += line_size;
       block += line_size;
   }
}

/* next one assumes that ((line_size % 8) == 0) */
static void put_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    register int i;
    register vector unsigned char pixelsv1, pixelsv2, pixelsavg;
    register vector unsigned char blockv, temp1, temp2;
    register vector unsigned short pixelssum1, pixelssum2, temp3;
    register const vector unsigned char vczero = (const vector unsigned char)vec_splat_u8(0);
    register const vector unsigned short vctwo = (const vector unsigned short)vec_splat_u16(2);

    temp1 = vec_ld(0, pixels);
    temp2 = vec_ld(16, pixels);
    pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
    if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F) {
        pixelsv2 = temp2;
    } else {
        pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
    }
    pixelsv1 = vec_mergeh(vczero, pixelsv1);
    pixelsv2 = vec_mergeh(vczero, pixelsv2);
    pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                         (vector unsigned short)pixelsv2);
    pixelssum1 = vec_add(pixelssum1, vctwo);

    for (i = 0; i < h ; i++) {
        int rightside = ((unsigned long)block & 0x0000000F);
        blockv = vec_ld(0, block);

        temp1 = vec_ld(line_size, pixels);
        temp2 = vec_ld(line_size + 16, pixels);
        pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
        if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F) {
            pixelsv2 = temp2;
        } else {
            pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(line_size + 1, pixels));
        }

        pixelsv1 = vec_mergeh(vczero, pixelsv1);
        pixelsv2 = vec_mergeh(vczero, pixelsv2);
        pixelssum2 = vec_add((vector unsigned short)pixelsv1,
                             (vector unsigned short)pixelsv2);
        temp3 = vec_add(pixelssum1, pixelssum2);
        temp3 = vec_sra(temp3, vctwo);
        pixelssum1 = vec_add(pixelssum2, vctwo);
        pixelsavg = vec_packsu(temp3, (vector unsigned short) vczero);

        if (rightside) {
            blockv = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
        } else {
            blockv = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
        }

        vec_st(blockv, 0, block);

        block += line_size;
        pixels += line_size;
    }
}

/* next one assumes that ((line_size % 8) == 0) */
static void put_no_rnd_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    register int i;
    register vector unsigned char pixelsv1, pixelsv2, pixelsavg;
    register vector unsigned char blockv, temp1, temp2;
    register vector unsigned short pixelssum1, pixelssum2, temp3;
    register const vector unsigned char vczero = (const vector unsigned char)vec_splat_u8(0);
    register const vector unsigned short vcone = (const vector unsigned short)vec_splat_u16(1);
    register const vector unsigned short vctwo = (const vector unsigned short)vec_splat_u16(2);

    temp1 = vec_ld(0, pixels);
    temp2 = vec_ld(16, pixels);
    pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
    if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F) {
        pixelsv2 = temp2;
    } else {
        pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
    }
    pixelsv1 = vec_mergeh(vczero, pixelsv1);
    pixelsv2 = vec_mergeh(vczero, pixelsv2);
    pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                         (vector unsigned short)pixelsv2);
    pixelssum1 = vec_add(pixelssum1, vcone);

    for (i = 0; i < h ; i++) {
        int rightside = ((unsigned long)block & 0x0000000F);
        blockv = vec_ld(0, block);

        temp1 = vec_ld(line_size, pixels);
        temp2 = vec_ld(line_size + 16, pixels);
        pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
        if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F) {
            pixelsv2 = temp2;
        } else {
            pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(line_size + 1, pixels));
        }

        pixelsv1 = vec_mergeh(vczero, pixelsv1);
        pixelsv2 = vec_mergeh(vczero, pixelsv2);
        pixelssum2 = vec_add((vector unsigned short)pixelsv1,
                             (vector unsigned short)pixelsv2);
        temp3 = vec_add(pixelssum1, pixelssum2);
        temp3 = vec_sra(temp3, vctwo);
        pixelssum1 = vec_add(pixelssum2, vcone);
        pixelsavg = vec_packsu(temp3, (vector unsigned short) vczero);

        if (rightside) {
            blockv = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
        } else {
            blockv = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
        }

        vec_st(blockv, 0, block);

        block += line_size;
        pixels += line_size;
    }
}

/* next one assumes that ((line_size % 16) == 0) */
static void put_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
    register int i;
    register vector unsigned char pixelsv1, pixelsv2, pixelsv3, pixelsv4;
    register vector unsigned char blockv, temp1, temp2;
    register vector unsigned short temp3, temp4,
        pixelssum1, pixelssum2, pixelssum3, pixelssum4;
    register const vector unsigned char vczero = (const vector unsigned char)vec_splat_u8(0);
    register const vector unsigned short vctwo = (const vector unsigned short)vec_splat_u16(2);

    temp1 = vec_ld(0, pixels);
    temp2 = vec_ld(16, pixels);
    pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
    if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F) {
        pixelsv2 = temp2;
    } else {
        pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
    }
    pixelsv3 = vec_mergel(vczero, pixelsv1);
    pixelsv4 = vec_mergel(vczero, pixelsv2);
    pixelsv1 = vec_mergeh(vczero, pixelsv1);
    pixelsv2 = vec_mergeh(vczero, pixelsv2);
    pixelssum3 = vec_add((vector unsigned short)pixelsv3,
                         (vector unsigned short)pixelsv4);
    pixelssum3 = vec_add(pixelssum3, vctwo);
    pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                         (vector unsigned short)pixelsv2);
    pixelssum1 = vec_add(pixelssum1, vctwo);

    for (i = 0; i < h ; i++) {
        blockv = vec_ld(0, block);

        temp1 = vec_ld(line_size, pixels);
        temp2 = vec_ld(line_size + 16, pixels);
        pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
        if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F) {
            pixelsv2 = temp2;
        } else {
            pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(line_size + 1, pixels));
        }

        pixelsv3 = vec_mergel(vczero, pixelsv1);
        pixelsv4 = vec_mergel(vczero, pixelsv2);
        pixelsv1 = vec_mergeh(vczero, pixelsv1);
        pixelsv2 = vec_mergeh(vczero, pixelsv2);

        pixelssum4 = vec_add((vector unsigned short)pixelsv3,
                             (vector unsigned short)pixelsv4);
        pixelssum2 = vec_add((vector unsigned short)pixelsv1,
                             (vector unsigned short)pixelsv2);
        temp4 = vec_add(pixelssum3, pixelssum4);
        temp4 = vec_sra(temp4, vctwo);
        temp3 = vec_add(pixelssum1, pixelssum2);
        temp3 = vec_sra(temp3, vctwo);

        pixelssum3 = vec_add(pixelssum4, vctwo);
        pixelssum1 = vec_add(pixelssum2, vctwo);

        blockv = vec_packsu(temp3, temp4);

        vec_st(blockv, 0, block);

        block += line_size;
        pixels += line_size;
    }
}

/* next one assumes that ((line_size % 16) == 0) */
static void put_no_rnd_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
    register int i;
    register vector unsigned char pixelsv1, pixelsv2, pixelsv3, pixelsv4;
    register vector unsigned char blockv, temp1, temp2;
    register vector unsigned short temp3, temp4,
        pixelssum1, pixelssum2, pixelssum3, pixelssum4;
    register const vector unsigned char vczero = (const vector unsigned char)vec_splat_u8(0);
    register const vector unsigned short vcone = (const vector unsigned short)vec_splat_u16(1);
    register const vector unsigned short vctwo = (const vector unsigned short)vec_splat_u16(2);

    temp1 = vec_ld(0, pixels);
    temp2 = vec_ld(16, pixels);
    pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
    if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F) {
        pixelsv2 = temp2;
    } else {
        pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
    }
    pixelsv3 = vec_mergel(vczero, pixelsv1);
    pixelsv4 = vec_mergel(vczero, pixelsv2);
    pixelsv1 = vec_mergeh(vczero, pixelsv1);
    pixelsv2 = vec_mergeh(vczero, pixelsv2);
    pixelssum3 = vec_add((vector unsigned short)pixelsv3,
                         (vector unsigned short)pixelsv4);
    pixelssum3 = vec_add(pixelssum3, vcone);
    pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                         (vector unsigned short)pixelsv2);
    pixelssum1 = vec_add(pixelssum1, vcone);

    for (i = 0; i < h ; i++) {
        blockv = vec_ld(0, block);

        temp1 = vec_ld(line_size, pixels);
        temp2 = vec_ld(line_size + 16, pixels);
        pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
        if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F) {
            pixelsv2 = temp2;
        } else {
            pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(line_size + 1, pixels));
        }

        pixelsv3 = vec_mergel(vczero, pixelsv1);
        pixelsv4 = vec_mergel(vczero, pixelsv2);
        pixelsv1 = vec_mergeh(vczero, pixelsv1);
        pixelsv2 = vec_mergeh(vczero, pixelsv2);

        pixelssum4 = vec_add((vector unsigned short)pixelsv3,
                             (vector unsigned short)pixelsv4);
        pixelssum2 = vec_add((vector unsigned short)pixelsv1,
                             (vector unsigned short)pixelsv2);
        temp4 = vec_add(pixelssum3, pixelssum4);
        temp4 = vec_sra(temp4, vctwo);
        temp3 = vec_add(pixelssum1, pixelssum2);
        temp3 = vec_sra(temp3, vctwo);

        pixelssum3 = vec_add(pixelssum4, vcone);
        pixelssum1 = vec_add(pixelssum2, vcone);

        blockv = vec_packsu(temp3, temp4);

        vec_st(blockv, 0, block);

        block += line_size;
        pixels += line_size;
    }
}

static int hadamard8_diff8x8_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h){
    int sum;
    register const vector unsigned char vzero =
                            (const vector unsigned char)vec_splat_u8(0);
    register vector signed short temp0, temp1, temp2, temp3, temp4,
                                 temp5, temp6, temp7;
    {
    register const vector signed short vprod1 =(const vector signed short)
                                               { 1,-1, 1,-1, 1,-1, 1,-1 };
    register const vector signed short vprod2 =(const vector signed short)
                                               { 1, 1,-1,-1, 1, 1,-1,-1 };
    register const vector signed short vprod3 =(const vector signed short)
                                               { 1, 1, 1, 1,-1,-1,-1,-1 };
    register const vector unsigned char perm1 = (const vector unsigned char)
        {0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
         0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D};
    register const vector unsigned char perm2 = (const vector unsigned char)
        {0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03,
         0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B};
    register const vector unsigned char perm3 = (const vector unsigned char)
        {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
         0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

#define ONEITERBUTTERFLY(i, res)                                          \
    {                                                                     \
    register vector unsigned char src1, src2, srcO;                   \
    register vector unsigned char dst1, dst2, dstO;                   \
    register vector signed short srcV, dstV;                          \
    register vector signed short but0, but1, but2, op1, op2, op3;     \
    src1 = vec_ld(stride * i, src);                                   \
    src2 = vec_ld((stride * i) + 15, src);                            \
    srcO = vec_perm(src1, src2, vec_lvsl(stride * i, src));           \
    dst1 = vec_ld(stride * i, dst);                                   \
    dst2 = vec_ld((stride * i) + 15, dst);                            \
    dstO = vec_perm(dst1, dst2, vec_lvsl(stride * i, dst));           \
    /* promote the unsigned chars to signed shorts */                 \
    /* we're in the 8x8 function, we only care for the first 8 */     \
    srcV = (vector signed short)vec_mergeh((vector signed char)vzero, \
           (vector signed char)srcO);                                 \
    dstV = (vector signed short)vec_mergeh((vector signed char)vzero, \
           (vector signed char)dstO);                                 \
    /* subtractions inside the first butterfly */                     \
    but0 = vec_sub(srcV, dstV);                                       \
    op1  = vec_perm(but0, but0, perm1);                               \
    but1 = vec_mladd(but0, vprod1, op1);                              \
    op2  = vec_perm(but1, but1, perm2);                               \
    but2 = vec_mladd(but1, vprod2, op2);                              \
    op3  = vec_perm(but2, but2, perm3);                               \
    res  = vec_mladd(but2, vprod3, op3);                              \
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
    register vector signed short line0 = vec_add(temp0, temp1);
    register vector signed short line1 = vec_sub(temp0, temp1);
    register vector signed short line2 = vec_add(temp2, temp3);
    register vector signed short line3 = vec_sub(temp2, temp3);
    register vector signed short line4 = vec_add(temp4, temp5);
    register vector signed short line5 = vec_sub(temp4, temp5);
    register vector signed short line6 = vec_add(temp6, temp7);
    register vector signed short line7 = vec_sub(temp6, temp7);

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
    vsum = vec_sums(vsum, (vector signed int)vzero);
    vsum = vec_splat(vsum, 3);
    vec_ste(vsum, 0, &sum);
    }
    return sum;
}

/*
16x8 works with 16 elements; it allows to avoid replicating loads, and
give the compiler more rooms for scheduling.  It's only used from
inside hadamard8_diff16_altivec.

Unfortunately, it seems gcc-3.3 is a bit dumb, and the compiled code has a LOT
of spill code, it seems gcc (unlike xlc) cannot keep everything in registers
by itself. The following code include hand-made registers allocation. It's not
clean, but on a 7450 the resulting code is much faster (best case fall from
700+ cycles to 550).

xlc doesn't add spill code, but it doesn't know how to schedule for the 7450,
and its code isn't much faster than gcc-3.3 on the 7450 (but uses 25% less
instructions...)

On the 970, the hand-made RA is still a win (around 690 vs. around 780), but
xlc goes to around 660 on the regular C code...
*/

static int hadamard8_diff16x8_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h) {
    int sum;
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
        (const vector unsigned char)vec_splat_u8(0);
    {
    register const vector signed short vprod1 __asm__ ("v16") =
        (const vector signed short){ 1,-1, 1,-1, 1,-1, 1,-1 };
    register const vector signed short vprod2 __asm__ ("v17") =
        (const vector signed short){ 1, 1,-1,-1, 1, 1,-1,-1 };
    register const vector signed short vprod3 __asm__ ("v18") =
        (const vector signed short){ 1, 1, 1, 1,-1,-1,-1,-1 };
    register const vector unsigned char perm1 __asm__ ("v19") =
        (const vector unsigned char)
        {0x02, 0x03, 0x00, 0x01, 0x06, 0x07, 0x04, 0x05,
         0x0A, 0x0B, 0x08, 0x09, 0x0E, 0x0F, 0x0C, 0x0D};
    register const vector unsigned char perm2 __asm__ ("v20") =
        (const vector unsigned char)
        {0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03,
         0x0C, 0x0D, 0x0E, 0x0F, 0x08, 0x09, 0x0A, 0x0B};
    register const vector unsigned char perm3 __asm__ ("v21") =
        (const vector unsigned char)
        {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
         0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

#define ONEITERBUTTERFLY(i, res1, res2)                               \
    {                                                                 \
    register vector unsigned char src1 __asm__ ("v22"),               \
                                  src2 __asm__ ("v23"),               \
                                  dst1 __asm__ ("v24"),               \
                                  dst2 __asm__ ("v25"),               \
                                  srcO __asm__ ("v22"),               \
                                  dstO __asm__ ("v23");               \
                                                                      \
    register vector signed short  srcV  __asm__ ("v24"),              \
                                  dstV  __asm__ ("v25"),              \
                                  srcW  __asm__ ("v26"),              \
                                  dstW  __asm__ ("v27"),              \
                                  but0  __asm__ ("v28"),              \
                                  but0S __asm__ ("v29"),              \
                                  op1   __asm__ ("v30"),              \
                                  but1  __asm__ ("v22"),              \
                                  op1S  __asm__ ("v23"),              \
                                  but1S __asm__ ("v24"),              \
                                  op2   __asm__ ("v25"),              \
                                  but2  __asm__ ("v26"),              \
                                  op2S  __asm__ ("v27"),              \
                                  but2S __asm__ ("v28"),              \
                                  op3   __asm__ ("v29"),              \
                                  op3S  __asm__ ("v30");              \
                                                                      \
    src1 = vec_ld(stride * i, src);                                   \
    src2 = vec_ld((stride * i) + 16, src);                            \
    srcO = vec_perm(src1, src2, vec_lvsl(stride * i, src));           \
    dst1 = vec_ld(stride * i, dst);                                   \
    dst2 = vec_ld((stride * i) + 16, dst);                            \
    dstO = vec_perm(dst1, dst2, vec_lvsl(stride * i, dst));           \
    /* promote the unsigned chars to signed shorts */                 \
    srcV = (vector signed short)vec_mergeh((vector signed char)vzero, \
           (vector signed char)srcO);                                 \
    dstV = (vector signed short)vec_mergeh((vector signed char)vzero, \
           (vector signed char)dstO);                                 \
    srcW = (vector signed short)vec_mergel((vector signed char)vzero, \
           (vector signed char)srcO);                                 \
    dstW = (vector signed short)vec_mergel((vector signed char)vzero, \
           (vector signed char)dstO);                                 \
    /* subtractions inside the first butterfly */                     \
    but0 = vec_sub(srcV, dstV);                                       \
    but0S = vec_sub(srcW, dstW);                                      \
    op1 = vec_perm(but0, but0, perm1);                                \
    but1 = vec_mladd(but0, vprod1, op1);                              \
    op1S = vec_perm(but0S, but0S, perm1);                             \
    but1S = vec_mladd(but0S, vprod1, op1S);                           \
    op2 = vec_perm(but1, but1, perm2);                                \
    but2 = vec_mladd(but1, vprod2, op2);                              \
    op2S = vec_perm(but1S, but1S, perm2);                             \
    but2S = vec_mladd(but1S, vprod2, op2S);                           \
    op3 = vec_perm(but2, but2, perm3);                                \
    res1 = vec_mladd(but2, vprod3, op3);                              \
    op3S = vec_perm(but2S, but2S, perm3);                             \
    res2 = vec_mladd(but2S, vprod3, op3S);                            \
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
    register vector signed short line0S, line1S, line2S, line3S, line4S,
                                 line5S, line6S, line7S, line0BS,line2BS,
                                 line1BS,line3BS,line4BS,line6BS,line5BS,
                                 line7BS,line0CS,line4CS,line1CS,line5CS,
                                 line2CS,line6CS,line3CS,line7CS;

    register vector signed short line0 = vec_add(temp0, temp1);
    register vector signed short line1 = vec_sub(temp0, temp1);
    register vector signed short line2 = vec_add(temp2, temp3);
    register vector signed short line3 = vec_sub(temp2, temp3);
    register vector signed short line4 = vec_add(temp4, temp5);
    register vector signed short line5 = vec_sub(temp4, temp5);
    register vector signed short line6 = vec_add(temp6, temp7);
    register vector signed short line7 = vec_sub(temp6, temp7);

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

    line0S = vec_add(temp0S, temp1S);
    line1S = vec_sub(temp0S, temp1S);
    line2S = vec_add(temp2S, temp3S);
    line3S = vec_sub(temp2S, temp3S);
    line4S = vec_add(temp4S, temp5S);
    line5S = vec_sub(temp4S, temp5S);
    line6S = vec_add(temp6S, temp7S);
    line7S = vec_sub(temp6S, temp7S);

    line0BS = vec_add(line0S, line2S);
    line2BS = vec_sub(line0S, line2S);
    line1BS = vec_add(line1S, line3S);
    line3BS = vec_sub(line1S, line3S);
    line4BS = vec_add(line4S, line6S);
    line6BS = vec_sub(line4S, line6S);
    line5BS = vec_add(line5S, line7S);
    line7BS = vec_sub(line5S, line7S);

    line0CS = vec_add(line0BS, line4BS);
    line4CS = vec_sub(line0BS, line4BS);
    line1CS = vec_add(line1BS, line5BS);
    line5CS = vec_sub(line1BS, line5BS);
    line2CS = vec_add(line2BS, line6BS);
    line6CS = vec_sub(line2BS, line6BS);
    line3CS = vec_add(line3BS, line7BS);
    line7CS = vec_sub(line3BS, line7BS);

    vsum = vec_sum4s(vec_abs(line0CS), vsum);
    vsum = vec_sum4s(vec_abs(line1CS), vsum);
    vsum = vec_sum4s(vec_abs(line2CS), vsum);
    vsum = vec_sum4s(vec_abs(line3CS), vsum);
    vsum = vec_sum4s(vec_abs(line4CS), vsum);
    vsum = vec_sum4s(vec_abs(line5CS), vsum);
    vsum = vec_sum4s(vec_abs(line6CS), vsum);
    vsum = vec_sum4s(vec_abs(line7CS), vsum);
    vsum = vec_sums(vsum, (vector signed int)vzero);
    vsum = vec_splat(vsum, 3);
    vec_ste(vsum, 0, &sum);
    }
    return sum;
}

static int hadamard8_diff16_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h){
    int score;
    score = hadamard8_diff16x8_altivec(s, dst, src, stride, 8);
    if (h==16) {
        dst += 8*stride;
        src += 8*stride;
        score += hadamard8_diff16x8_altivec(s, dst, src, stride, 8);
    }
    return score;
}

static void vorbis_inverse_coupling_altivec(float *mag, float *ang,
                                            int blocksize)
{
    int i;
    vector float m, a;
    vector bool int t0, t1;
    const vector unsigned int v_31 = //XXX
        vec_add(vec_add(vec_splat_u32(15),vec_splat_u32(15)),vec_splat_u32(1));
    for (i = 0; i < blocksize; i += 4) {
        m = vec_ld(0, mag+i);
        a = vec_ld(0, ang+i);
        t0 = vec_cmple(m, (vector float)vec_splat_u32(0));
        t1 = vec_cmple(a, (vector float)vec_splat_u32(0));
        a = vec_xor(a, (vector float) vec_sl((vector unsigned int)t0, v_31));
        t0 = (vector bool int)vec_and(a, t1);
        t1 = (vector bool int)vec_andc(a, t1);
        a = vec_sub(m, (vector float)t1);
        m = vec_add(m, (vector float)t0);
        vec_stl(a, 0, ang+i);
        vec_stl(m, 0, mag+i);
    }
}

/* next one assumes that ((line_size % 8) == 0) */
static void avg_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
    register int i;
    register vector unsigned char pixelsv1, pixelsv2, pixelsavg;
    register vector unsigned char blockv, temp1, temp2, blocktemp;
    register vector unsigned short pixelssum1, pixelssum2, temp3;

    register const vector unsigned char vczero = (const vector unsigned char)
                                        vec_splat_u8(0);
    register const vector unsigned short vctwo = (const vector unsigned short)
                                        vec_splat_u16(2);

    temp1 = vec_ld(0, pixels);
    temp2 = vec_ld(16, pixels);
    pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
    if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F) {
        pixelsv2 = temp2;
    } else {
        pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
    }
    pixelsv1 = vec_mergeh(vczero, pixelsv1);
    pixelsv2 = vec_mergeh(vczero, pixelsv2);
    pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                         (vector unsigned short)pixelsv2);
    pixelssum1 = vec_add(pixelssum1, vctwo);

    for (i = 0; i < h ; i++) {
        int rightside = ((unsigned long)block & 0x0000000F);
        blockv = vec_ld(0, block);

        temp1 = vec_ld(line_size, pixels);
        temp2 = vec_ld(line_size + 16, pixels);
        pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
        if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F) {
            pixelsv2 = temp2;
        } else {
            pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(line_size + 1, pixels));
        }

        pixelsv1 = vec_mergeh(vczero, pixelsv1);
        pixelsv2 = vec_mergeh(vczero, pixelsv2);
        pixelssum2 = vec_add((vector unsigned short)pixelsv1,
                             (vector unsigned short)pixelsv2);
        temp3 = vec_add(pixelssum1, pixelssum2);
        temp3 = vec_sra(temp3, vctwo);
        pixelssum1 = vec_add(pixelssum2, vctwo);
        pixelsavg = vec_packsu(temp3, (vector unsigned short) vczero);

        if (rightside) {
            blocktemp = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
        } else {
            blocktemp = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
        }

        blockv = vec_avg(blocktemp, blockv);
        vec_st(blockv, 0, block);

        block += line_size;
        pixels += line_size;
    }
}

void ff_dsputil_init_altivec(DSPContext* c, AVCodecContext *avctx)
{
    const int high_bit_depth = avctx->bits_per_raw_sample > 8;

    c->pix_abs[0][1] = sad16_x2_altivec;
    c->pix_abs[0][2] = sad16_y2_altivec;
    c->pix_abs[0][3] = sad16_xy2_altivec;
    c->pix_abs[0][0] = sad16_altivec;
    c->pix_abs[1][0] = sad8_altivec;
    c->sad[0]= sad16_altivec;
    c->sad[1]= sad8_altivec;
    c->pix_norm1 = pix_norm1_altivec;
    c->sse[1]= sse8_altivec;
    c->sse[0]= sse16_altivec;
    c->pix_sum = pix_sum_altivec;
    c->diff_pixels = diff_pixels_altivec;
    c->add_bytes= add_bytes_altivec;
    if (!high_bit_depth) {
    c->get_pixels = get_pixels_altivec;
    c->clear_block = clear_block_altivec;
    c->put_pixels_tab[0][0] = ff_put_pixels16_altivec;
    /* the two functions do the same thing, so use the same code */
    c->put_no_rnd_pixels_tab[0][0] = ff_put_pixels16_altivec;
    c->avg_pixels_tab[0][0] = ff_avg_pixels16_altivec;
    c->avg_pixels_tab[1][0] = avg_pixels8_altivec;
    c->avg_pixels_tab[1][3] = avg_pixels8_xy2_altivec;
    c->put_pixels_tab[1][3] = put_pixels8_xy2_altivec;
    c->put_no_rnd_pixels_tab[1][3] = put_no_rnd_pixels8_xy2_altivec;
    c->put_pixels_tab[0][3] = put_pixels16_xy2_altivec;
    c->put_no_rnd_pixels_tab[0][3] = put_no_rnd_pixels16_xy2_altivec;
    }

    c->hadamard8_diff[0] = hadamard8_diff16_altivec;
    c->hadamard8_diff[1] = hadamard8_diff8x8_altivec;
    if (CONFIG_VORBIS_DECODER)
        c->vorbis_inverse_coupling = vorbis_inverse_coupling_altivec;
}
