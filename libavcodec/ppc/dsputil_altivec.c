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

