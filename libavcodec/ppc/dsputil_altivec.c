/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
#include "../dsputil.h"
#include "dsputil_altivec.h"

#if CONFIG_DARWIN
#include <sys/sysctl.h>
#endif

int pix_abs16x16_x2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int s, i;
    vector unsigned char *tv, zero;
    vector unsigned char pix1v, pix2v, pix2iv, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    s = 0;
    zero = vec_splat_u8(0);
    sad = vec_splat_u32(0);
    for(i=0;i<16;i++) {
        /*
           Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix2v: pix2[0]-pix2[15]	pix2iv: pix2[1]-pix2[16]
        */
        tv = (vector unsigned char *) pix1;
        pix1v = vec_perm(tv[0], tv[1], vec_lvsl(0, pix1));
        
        tv = (vector unsigned char *) &pix2[0];
        pix2v = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix2[0]));

        tv = (vector unsigned char *) &pix2[1];
        pix2iv = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix2[1]));

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

int pix_abs16x16_y2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int s, i;
    vector unsigned char *tv, zero;
    vector unsigned char pix1v, pix2v, pix3v, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;
    uint8_t *pix3 = pix2 + line_size;

    s = 0;
    zero = vec_splat_u8(0);
    sad = vec_splat_u32(0);

    /*
       Due to the fact that pix3 = pix2 + line_size, the pix3 of one
       iteration becomes pix2 in the next iteration. We can use this
       fact to avoid a potentially expensive unaligned read, each
       time around the loop.
       Read unaligned pixels into our vectors. The vectors are as follows:
       pix2v: pix2[0]-pix2[15]
       Split the pixel vectors into shorts
    */
    tv = (vector unsigned char *) &pix2[0];
    pix2v = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix2[0]));
    
    for(i=0;i<16;i++) {
        /*
           Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix3v: pix3[0]-pix3[15]
        */
        tv = (vector unsigned char *) pix1;
        pix1v = vec_perm(tv[0], tv[1], vec_lvsl(0, pix1));

        tv = (vector unsigned char *) &pix3[0];
        pix3v = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix3[0]));

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

int pix_abs16x16_xy2_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int s, i;
    uint8_t *pix3 = pix2 + line_size;
    vector unsigned char *tv, avgv, t5, zero;
    vector unsigned char pix1v, pix2v, pix3v, pix2iv, pix3iv;
    vector unsigned short pix2lv, pix2hv, pix2ilv, pix2ihv;
    vector unsigned short pix3lv, pix3hv, pix3ilv, pix3ihv;
    vector unsigned short avghv, avglv, two;
    vector unsigned short t1, t2, t3, t4;
    vector unsigned int sad;
    vector signed int sumdiffs;

    zero = vec_splat_u8(0);
    two = vec_splat_u16(2);
    sad = vec_splat_u32(0);
    
    s = 0;

    /*
       Due to the fact that pix3 = pix2 + line_size, the pix3 of one
       iteration becomes pix2 in the next iteration. We can use this
       fact to avoid a potentially expensive unaligned read, as well
       as some splitting, and vector addition each time around the loop.
       Read unaligned pixels into our vectors. The vectors are as follows:
       pix2v: pix2[0]-pix2[15]	pix2iv: pix2[1]-pix2[16]
       Split the pixel vectors into shorts
    */
    tv = (vector unsigned char *) &pix2[0];
    pix2v = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix2[0]));

    tv = (vector unsigned char *) &pix2[1];
    pix2iv = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix2[1]));

    pix2hv = (vector unsigned short) vec_mergeh(zero, pix2v);
    pix2lv = (vector unsigned short) vec_mergel(zero, pix2v);
    pix2ihv = (vector unsigned short) vec_mergeh(zero, pix2iv);
    pix2ilv = (vector unsigned short) vec_mergel(zero, pix2iv);
    t1 = vec_add(pix2hv, pix2ihv);
    t2 = vec_add(pix2lv, pix2ilv);
    
    for(i=0;i<16;i++) {
        /*
           Read unaligned pixels into our vectors. The vectors are as follows:
           pix1v: pix1[0]-pix1[15]
           pix3v: pix3[0]-pix3[15]	pix3iv: pix3[1]-pix3[16]
        */
        tv = (vector unsigned char *) pix1;
        pix1v = vec_perm(tv[0], tv[1], vec_lvsl(0, pix1));

        tv = (vector unsigned char *) &pix3[0];
        pix3v = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix3[0]));

        tv = (vector unsigned char *) &pix3[1];
        pix3iv = vec_perm(tv[0], tv[1], vec_lvsl(0, &pix3[1]));

        /*
          Note that Altivec does have vec_avg, but this works on vector pairs
          and rounds up. We could do avg(avg(a,b),avg(c,d)), but the rounding
          would mean that, for example, avg(3,0,0,1) = 2, when it should be 1.
          Instead, we have to split the pixel vectors into vectors of shorts,
          and do the averaging by hand.
        */

        /* Split the pixel vectors into shorts */
        pix3hv = (vector unsigned short) vec_mergeh(zero, pix3v);
        pix3lv = (vector unsigned short) vec_mergel(zero, pix3v);
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

int pix_abs16x16_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int i, s;
    vector unsigned char perm1, perm2, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad, zero;
    vector signed int sumdiffs;
    
    zero = (vector unsigned int) (0);
    sad = (vector unsigned int) (0);


    for(i=0;i<16;i++) {
	/* Read potentially unaligned pixels into t1 and t2 */
        perm1 = vec_lvsl(0, pix1);
        pix1v = (vector unsigned char *) pix1;
        perm2 = vec_lvsl(0, pix2);
        pix2v = (vector unsigned char *) pix2;
        t1 = vec_perm(pix1v[0], pix1v[1], perm1);
        t2 = vec_perm(pix2v[0], pix2v[1], perm2);
       
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

int pix_abs8x8_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int i, s;
    vector unsigned char perm1, perm2, permclear, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad, zero;
    vector signed int sumdiffs;

    zero = (vector unsigned int) (0);
    sad = (vector unsigned int) (0);
    permclear = (vector unsigned char) (255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0);

    for(i=0;i<8;i++) {
	/* Read potentially unaligned pixels into t1 and t2
	   Since we're reading 16 pixels, and actually only want 8,
	   mask out the last 8 pixels. The 0s don't change the sum. */
        perm1 = vec_lvsl(0, pix1);
        pix1v = (vector unsigned char *) pix1;
        perm2 = vec_lvsl(0, pix2);
        pix2v = (vector unsigned char *) pix2;
        t1 = vec_and(vec_perm(pix1v[0], pix1v[1], perm1), permclear);
        t2 = vec_and(vec_perm(pix2v[0], pix2v[1], perm2), permclear);

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

int pix_norm1_altivec(uint8_t *pix, int line_size)
{
    int s, i;
    vector unsigned char *tv, zero;
    vector unsigned char pixv;
    vector unsigned int sv;
    vector signed int sum;
        
    zero = vec_splat_u8(0);
    sv = vec_splat_u32(0);
    
    s = 0;
    for (i = 0; i < 16; i++) {
        /* Read in the potentially unaligned pixels */
        tv = (vector unsigned char *) pix;
        pixv = vec_perm(tv[0], tv[1], vec_lvsl(0, pix));

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


int pix_norm_altivec(uint8_t *pix1, uint8_t *pix2, int line_size)
{
    int s, i;
    vector unsigned char *tv, zero;
    vector unsigned char pix1v, pix2v, t5;
    vector unsigned int sv;
    vector signed int sum;

    zero = vec_splat_u8(0);
    sv = vec_splat_u32(0);
    s = 0;
    for (i = 0; i < 16; i++) {
        /* Read in the potentially unaligned pixels */
        tv = (vector unsigned char *) pix1;
        pix1v = vec_perm(tv[0], tv[1], vec_lvsl(0, pix1));

        tv = (vector unsigned char *) pix2;
        pix2v = vec_perm(tv[0], tv[1], vec_lvsl(0, pix2));

        /*
           Since we want to use unsigned chars, we can take advantage
           of the fact that abs(a-b)^2 = (a-b)^2.
        */
        
        /* Calculate a sum of abs differences vector */
        t5 = vec_sub(vec_max(pix1v, pix2v), vec_min(pix1v, pix2v));

        /* Square the values and add them to our sum */
        sv = vec_msum(t5, t5, sv);
        
        pix1 += line_size;
        pix2 += line_size;
    }
    /* Sum up the four partial sums, and put the result into s */
    sum = vec_sums((vector signed int) sv, (vector signed int) zero);
    sum = vec_splat(sum, 3);
    vec_ste(sum, 0, &s);
    return s;
}


int pix_sum_altivec(UINT8 * pix, int line_size)
{

    vector unsigned char perm, *pixv;
    vector unsigned char t1;
    vector unsigned int sad, zero;
    vector signed int sumdiffs;

    int s, i;

    zero = (vector unsigned int) (0);
    sad = (vector unsigned int) (0);
    
    for (i = 0; i < 16; i++) {
	/* Read the potentially unaligned 16 pixels into t1 */
        perm = vec_lvsl(0, pix);
        pixv = (vector unsigned char *) pix;
        t1 = vec_perm(pixv[0], pixv[1], perm);

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

void get_pixels_altivec(DCTELEM *restrict block, const UINT8 *pixels, int line_size)
{
    int i;
    vector unsigned char perm, bytes, *pixv;
    vector unsigned char zero = (vector unsigned char) (0);
    vector signed short shorts;

    for(i=0;i<8;i++)
    {
        // Read potentially unaligned pixels.
        // We're reading 16 pixels, and actually only want 8,
        // but we simply ignore the extras.
        perm = vec_lvsl(0, pixels);
        pixv = (vector unsigned char *) pixels;
        bytes = vec_perm(pixv[0], pixv[1], perm);

        // convert the bytes into shorts
        shorts = (vector signed short)vec_mergeh(zero, bytes);

        // save the data to the block, we assume the block is 16-byte aligned
        vec_st(shorts, i*16, (vector signed short*)block);

        pixels += line_size;
    }
}

void diff_pixels_altivec(DCTELEM *restrict block, const UINT8 *s1,
        const UINT8 *s2, int stride)
{
    int i;
    vector unsigned char perm, bytes, *pixv;
    vector unsigned char zero = (vector unsigned char) (0);
    vector signed short shorts1, shorts2;

    for(i=0;i<4;i++)
    {
        // Read potentially unaligned pixels
        // We're reading 16 pixels, and actually only want 8,
        // but we simply ignore the extras.
        perm = vec_lvsl(0, s1);
        pixv = (vector unsigned char *) s1;
        bytes = vec_perm(pixv[0], pixv[1], perm);

        // convert the bytes into shorts
        shorts1 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the same for the second block of pixels
        perm = vec_lvsl(0, s2);
        pixv = (vector unsigned char *) s2;
        bytes = vec_perm(pixv[0], pixv[1], perm);

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
        perm = vec_lvsl(0, s1);
        pixv = (vector unsigned char *) s1;
        bytes = vec_perm(pixv[0], pixv[1], perm);

        // convert the bytes into shorts
        shorts1 = (vector signed short)vec_mergeh(zero, bytes);

        // Do the same for the second block of pixels
        perm = vec_lvsl(0, s2);
        pixv = (vector unsigned char *) s2;
        bytes = vec_perm(pixv[0], pixv[1], perm);

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


int has_altivec(void)
{
#if CONFIG_DARWIN
    int sels[2] = {CTL_HW, HW_VECTORUNIT};
    int has_vu = 0;
    size_t len = sizeof(has_vu);
    int err;

    err = sysctl(sels, 2, &has_vu, &len, NULL, 0);

    if (err == 0) return (has_vu != 0);
#endif
    return 0;
}

