/*
 * Copyright (c) 2002 Brian Foley
 * Copyright (c) 2002 Dieter Shirley
 * Copyright (c) 2003-2004 Romain Dolbeau <romain@dolbeau.org>
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

#include "gcc_fixes.h"

#include "dsputil_altivec.h"

#ifdef CONFIG_DARWIN
#include <sys/sysctl.h>
#else /* CONFIG_DARWIN */
#ifdef __AMIGAOS4__
#include <exec/exec.h>
#include <interfaces/exec.h>
#include <proto/exec.h>
#else /* __AMIGAOS4__ */
#include <signal.h>
#include <setjmp.h>

static sigjmp_buf jmpbuf;
static volatile sig_atomic_t canjump = 0;

static void sigill_handler (int sig)
{
    if (!canjump) {
        signal (sig, SIG_DFL);
        raise (sig);
    }
    
    canjump = 0;
    siglongjmp (jmpbuf, 1);
}
#endif /* CONFIG_DARWIN */
#endif /* __AMIGAOS4__ */

int sad16_x2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned char zero = (const_vector unsigned char)vec_splat_u8(0);
    vector unsigned char *tv;
    vector unsigned char pix1v, pix2v, pix2iv, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    s = 0;
    sad = (vector unsigned int)vec_splat_u32(0);
    for(i=0;i<h;i++) {
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

int sad16_y2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned char zero = (const_vector unsigned char)vec_splat_u8(0);
    vector unsigned char *tv;
    vector unsigned char pix1v, pix2v, pix3v, avgv, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;
    uint8_t *pix3 = pix2 + line_size;

    s = 0;
    sad = (vector unsigned int)vec_splat_u32(0);

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
    
    for(i=0;i<h;i++) {
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

int sad16_xy2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    uint8_t *pix3 = pix2 + line_size;
    const_vector unsigned char zero = (const_vector unsigned char)vec_splat_u8(0);
    const_vector unsigned short two = (const_vector unsigned short)vec_splat_u16(2);
    vector unsigned char *tv, avgv, t5;
    vector unsigned char pix1v, pix2v, pix3v, pix2iv, pix3iv;
    vector unsigned short pix2lv, pix2hv, pix2ilv, pix2ihv;
    vector unsigned short pix3lv, pix3hv, pix3ilv, pix3ihv;
    vector unsigned short avghv, avglv;
    vector unsigned short t1, t2, t3, t4;
    vector unsigned int sad;
    vector signed int sumdiffs;

    sad = (vector unsigned int)vec_splat_u32(0);
    
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
    
    for(i=0;i<h;i++) {
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

int sad16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm1, perm2, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;
    
    sad = (vector unsigned int)vec_splat_u32(0);


    for(i=0;i<h;i++) {
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

int sad8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm1, perm2, permclear, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sad;
    vector signed int sumdiffs;

    sad = (vector unsigned int)vec_splat_u32(0);

    permclear = (vector unsigned char)AVV(255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0);

    for(i=0;i<h;i++) {
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
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char *tv;
    vector unsigned char pixv;
    vector unsigned int sv;
    vector signed int sum;
    
    sv = (vector unsigned int)vec_splat_u32(0);
    
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

/**
 * Sum of Squared Errors for a 8x8 block.
 * AltiVec-enhanced.
 * It's the sad8_altivec code above w/ squaring added.
 */
int sse8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm1, perm2, permclear, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sum;
    vector signed int sumsqr;
    
    sum = (vector unsigned int)vec_splat_u32(0);

    permclear = (vector unsigned char)AVV(255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0);

    
    for(i=0;i<h;i++) {
	/* Read potentially unaligned pixels into t1 and t2
	   Since we're reading 16 pixels, and actually only want 8,
	   mask out the last 8 pixels. The 0s don't change the sum. */
        perm1 = vec_lvsl(0, pix1);
        pix1v = (vector unsigned char *) pix1;
        perm2 = vec_lvsl(0, pix2);
        pix2v = (vector unsigned char *) pix2;
        t1 = vec_and(vec_perm(pix1v[0], pix1v[1], perm1), permclear);
        t2 = vec_and(vec_perm(pix2v[0], pix2v[1], perm2), permclear);

        /*
          Since we want to use unsigned chars, we can take advantage
          of the fact that abs(a-b)^2 = (a-b)^2.
        */
        
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
int sse16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)
{
    int i;
    int s __attribute__((aligned(16)));
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm1, perm2, *pix1v, *pix2v;
    vector unsigned char t1, t2, t3,t4, t5;
    vector unsigned int sum;
    vector signed int sumsqr;
    
    sum = (vector unsigned int)vec_splat_u32(0);
    
    for(i=0;i<h;i++) {
	/* Read potentially unaligned pixels into t1 and t2 */
        perm1 = vec_lvsl(0, pix1);
        pix1v = (vector unsigned char *) pix1;
        perm2 = vec_lvsl(0, pix2);
        pix2v = (vector unsigned char *) pix2;
        t1 = vec_perm(pix1v[0], pix1v[1], perm1);
        t2 = vec_perm(pix2v[0], pix2v[1], perm2);

        /*
          Since we want to use unsigned chars, we can take advantage
          of the fact that abs(a-b)^2 = (a-b)^2.
        */
        
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

int pix_sum_altivec(uint8_t * pix, int line_size)
{
    const_vector unsigned int zero = (const_vector unsigned int)vec_splat_u32(0);
    vector unsigned char perm, *pixv;
    vector unsigned char t1;
    vector unsigned int sad;
    vector signed int sumdiffs;

    int i;
    int s __attribute__((aligned(16)));
    
    sad = (vector unsigned int)vec_splat_u32(0);
    
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

void get_pixels_altivec(DCTELEM *restrict block, const uint8_t *pixels, int line_size)
{
    int i;
    vector unsigned char perm, bytes, *pixv;
    const_vector unsigned char zero = (const_vector unsigned char)vec_splat_u8(0);
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

void diff_pixels_altivec(DCTELEM *restrict block, const uint8_t *s1,
        const uint8_t *s2, int stride)
{
    int i;
    vector unsigned char perm, bytes, *pixv;
    const_vector unsigned char zero = (const_vector unsigned char)vec_splat_u8(0);
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

void add_bytes_altivec(uint8_t *dst, uint8_t *src, int w) {
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int i;
    for(i=0; i+7<w; i++){
        dst[i+0] += src[i+0];
        dst[i+1] += src[i+1];
        dst[i+2] += src[i+2];
        dst[i+3] += src[i+3];
        dst[i+4] += src[i+4];
        dst[i+5] += src[i+5];
        dst[i+6] += src[i+6];
        dst[i+7] += src[i+7];
    }
    for(; i<w; i++)
        dst[i+0] += src[i+0];
#else /* ALTIVEC_USE_REFERENCE_C_CODE */
    register int i;
    register vector unsigned char vdst, vsrc;
    
    /* dst and src are 16 bytes-aligned (guaranteed) */
    for(i = 0 ; (i + 15) < w ; i++)
    {
      vdst = vec_ld(i << 4, (unsigned char*)dst);
      vsrc = vec_ld(i << 4, (unsigned char*)src);
      vdst = vec_add(vsrc, vdst);
      vec_st(vdst, i << 4, (unsigned char*)dst);
    }
    /* if w is not a multiple of 16 */
    for (; (i < w) ; i++)
    {
      dst[i] = src[i];
    }
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 16) == 0) */
void put_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_put_pixels16_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int i;

POWERPC_PERF_START_COUNT(altivec_put_pixels16_num, 1);

    for(i=0; i<h; i++) {
      *((uint32_t*)(block)) = LD32(pixels);
      *((uint32_t*)(block+4)) = LD32(pixels+4);
      *((uint32_t*)(block+8)) = LD32(pixels+8);
      *((uint32_t*)(block+12)) = LD32(pixels+12);
      pixels+=line_size;
      block +=line_size;
    }

POWERPC_PERF_STOP_COUNT(altivec_put_pixels16_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
    register vector unsigned char pixelsv1, pixelsv2;
    register vector unsigned char pixelsv1B, pixelsv2B;
    register vector unsigned char pixelsv1C, pixelsv2C;
    register vector unsigned char pixelsv1D, pixelsv2D;

    register vector unsigned char perm = vec_lvsl(0, pixels);
    int i;
    register int line_size_2 = line_size << 1;
    register int line_size_3 = line_size + line_size_2;
    register int line_size_4 = line_size << 2;

POWERPC_PERF_START_COUNT(altivec_put_pixels16_num, 1);
// hand-unrolling the loop by 4 gains about 15%
// mininum execution time goes from 74 to 60 cycles
// it's faster than -funroll-loops, but using
// -funroll-loops w/ this is bad - 74 cycles again.
// all this is on a 7450, tuning for the 7450
#if 0
    for(i=0; i<h; i++) {
      pixelsv1 = vec_ld(0, (unsigned char*)pixels);
      pixelsv2 = vec_ld(16, (unsigned char*)pixels);
      vec_st(vec_perm(pixelsv1, pixelsv2, perm),
             0, (unsigned char*)block);
      pixels+=line_size;
      block +=line_size;
    }
#else
    for(i=0; i<h; i+=4) {
      pixelsv1 = vec_ld(0, (unsigned char*)pixels);
      pixelsv2 = vec_ld(16, (unsigned char*)pixels);
      pixelsv1B = vec_ld(line_size, (unsigned char*)pixels);
      pixelsv2B = vec_ld(16 + line_size, (unsigned char*)pixels);
      pixelsv1C = vec_ld(line_size_2, (unsigned char*)pixels);
      pixelsv2C = vec_ld(16 + line_size_2, (unsigned char*)pixels);
      pixelsv1D = vec_ld(line_size_3, (unsigned char*)pixels);
      pixelsv2D = vec_ld(16 + line_size_3, (unsigned char*)pixels);
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
#endif
POWERPC_PERF_STOP_COUNT(altivec_put_pixels16_num, 1);

#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 16) == 0) */
#define op_avg(a,b)  a = ( ((a)|(b)) - ((((a)^(b))&0xFEFEFEFEUL)>>1) )
void avg_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_avg_pixels16_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int i;

POWERPC_PERF_START_COUNT(altivec_avg_pixels16_num, 1);

    for(i=0; i<h; i++) {
      op_avg(*((uint32_t*)(block)),LD32(pixels));
      op_avg(*((uint32_t*)(block+4)),LD32(pixels+4));
      op_avg(*((uint32_t*)(block+8)),LD32(pixels+8));
      op_avg(*((uint32_t*)(block+12)),LD32(pixels+12));
      pixels+=line_size;
      block +=line_size;
    }

POWERPC_PERF_STOP_COUNT(altivec_avg_pixels16_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
    register vector unsigned char pixelsv1, pixelsv2, pixelsv, blockv;
    register vector unsigned char perm = vec_lvsl(0, pixels);
    int i;

POWERPC_PERF_START_COUNT(altivec_avg_pixels16_num, 1);

    for(i=0; i<h; i++) {
      pixelsv1 = vec_ld(0, (unsigned char*)pixels);
      pixelsv2 = vec_ld(16, (unsigned char*)pixels);
      blockv = vec_ld(0, block);
      pixelsv = vec_perm(pixelsv1, pixelsv2, perm);
      blockv = vec_avg(blockv,pixelsv);
      vec_st(blockv, 0, (unsigned char*)block);
      pixels+=line_size;
      block +=line_size;
    }

POWERPC_PERF_STOP_COUNT(altivec_avg_pixels16_num, 1);

#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 8) == 0) */
void avg_pixels8_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_avg_pixels8_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int i;
POWERPC_PERF_START_COUNT(altivec_avg_pixels8_num, 1);
    for (i = 0; i < h; i++) {
        *((uint32_t *) (block)) =
            (((*((uint32_t *) (block))) |
              ((((const struct unaligned_32 *) (pixels))->l))) -
             ((((*((uint32_t *) (block))) ^
                ((((const struct unaligned_32 *) (pixels))->
                  l))) & 0xFEFEFEFEUL) >> 1));
        *((uint32_t *) (block + 4)) =
            (((*((uint32_t *) (block + 4))) |
              ((((const struct unaligned_32 *) (pixels + 4))->l))) -
             ((((*((uint32_t *) (block + 4))) ^
                ((((const struct unaligned_32 *) (pixels +
                                                  4))->
                  l))) & 0xFEFEFEFEUL) >> 1));
        pixels += line_size;
        block += line_size;
    }
POWERPC_PERF_STOP_COUNT(altivec_avg_pixels8_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
    register vector unsigned char pixelsv1, pixelsv2, pixelsv, blockv;
    int i;

POWERPC_PERF_START_COUNT(altivec_avg_pixels8_num, 1);
 
   for (i = 0; i < h; i++) {
     /*
       block is 8 bytes-aligned, so we're either in the
       left block (16 bytes-aligned) or in the right block (not)
     */
     int rightside = ((unsigned long)block & 0x0000000F);
     
     blockv = vec_ld(0, block);
     pixelsv1 = vec_ld(0, (unsigned char*)pixels);
     pixelsv2 = vec_ld(16, (unsigned char*)pixels);
     pixelsv = vec_perm(pixelsv1, pixelsv2, vec_lvsl(0, pixels));
     
     if (rightside)
     {
       pixelsv = vec_perm(blockv, pixelsv, vcprm(0,1,s0,s1));
     }
     else
     {
       pixelsv = vec_perm(blockv, pixelsv, vcprm(s0,s1,2,3));
     }
     
     blockv = vec_avg(blockv, pixelsv);

     vec_st(blockv, 0, block);
     
     pixels += line_size;
     block += line_size;
   }
   
POWERPC_PERF_STOP_COUNT(altivec_avg_pixels8_num, 1);
 
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 8) == 0) */
void put_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_put_pixels8_xy2_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int j;
POWERPC_PERF_START_COUNT(altivec_put_pixels8_xy2_num, 1);
    for (j = 0; j < 2; j++) {
      int i;
      const uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
      const uint32_t b =
        (((const struct unaligned_32 *) (pixels + 1))->l);
      uint32_t l0 =
        (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
      uint32_t h0 =
        ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
      uint32_t l1, h1;
      pixels += line_size;
      for (i = 0; i < h; i += 2) {
        uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
        uint32_t b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l1 = (a & 0x03030303UL) + (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
        a = (((const struct unaligned_32 *) (pixels))->l);
        b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
      } pixels += 4 - line_size * (h + 1);
      block += 4 - line_size * h;
    }

POWERPC_PERF_STOP_COUNT(altivec_put_pixels8_xy2_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
   register int i;
   register vector unsigned char
     pixelsv1, pixelsv2,
     pixelsavg;
   register vector unsigned char
     blockv, temp1, temp2;
   register vector unsigned short
     pixelssum1, pixelssum2, temp3;
   register const_vector unsigned char vczero = (const_vector unsigned char)vec_splat_u8(0);
   register const_vector unsigned short vctwo = (const_vector unsigned short)vec_splat_u16(2);
   
   temp1 = vec_ld(0, pixels);
   temp2 = vec_ld(16, pixels);
   pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
   if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F)
   {
     pixelsv2 = temp2;
   }
   else
   {
     pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
   }
   pixelsv1 = vec_mergeh(vczero, pixelsv1);
   pixelsv2 = vec_mergeh(vczero, pixelsv2);
   pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                        (vector unsigned short)pixelsv2);
   pixelssum1 = vec_add(pixelssum1, vctwo);
   
POWERPC_PERF_START_COUNT(altivec_put_pixels8_xy2_num, 1); 
   for (i = 0; i < h ; i++) {
     int rightside = ((unsigned long)block & 0x0000000F);
     blockv = vec_ld(0, block);

     temp1 = vec_ld(line_size, pixels);
     temp2 = vec_ld(line_size + 16, pixels);
     pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
     if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F)
     {
       pixelsv2 = temp2;
     }
     else
     {
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
     
     if (rightside)
     {
       blockv = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
     }
     else
     {
       blockv = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
     }
     
     vec_st(blockv, 0, block);
     
     block += line_size;
     pixels += line_size;
   }
   
POWERPC_PERF_STOP_COUNT(altivec_put_pixels8_xy2_num, 1);
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 8) == 0) */
void put_no_rnd_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_put_no_rnd_pixels8_xy2_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int j;
POWERPC_PERF_START_COUNT(altivec_put_no_rnd_pixels8_xy2_num, 1);
    for (j = 0; j < 2; j++) {
      int i;
      const uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
      const uint32_t b =
        (((const struct unaligned_32 *) (pixels + 1))->l);
      uint32_t l0 =
        (a & 0x03030303UL) + (b & 0x03030303UL) + 0x01010101UL;
      uint32_t h0 =
        ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
      uint32_t l1, h1;
      pixels += line_size;
      for (i = 0; i < h; i += 2) {
        uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
        uint32_t b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l1 = (a & 0x03030303UL) + (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
        a = (((const struct unaligned_32 *) (pixels))->l);
        b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x01010101UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
      } pixels += 4 - line_size * (h + 1);
      block += 4 - line_size * h;
    }
    
POWERPC_PERF_STOP_COUNT(altivec_put_no_rnd_pixels8_xy2_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
   register int i;
   register vector unsigned char
     pixelsv1, pixelsv2,
     pixelsavg;
   register vector unsigned char
     blockv, temp1, temp2;
   register vector unsigned short
     pixelssum1, pixelssum2, temp3;
   register const_vector unsigned char vczero = (const_vector unsigned char)vec_splat_u8(0);
   register const_vector unsigned short vcone = (const_vector unsigned short)vec_splat_u16(1);
   register const_vector unsigned short vctwo = (const_vector unsigned short)vec_splat_u16(2);
   
   temp1 = vec_ld(0, pixels);
   temp2 = vec_ld(16, pixels);
   pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
   if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F)
   {
     pixelsv2 = temp2;
   }
   else
   {
     pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
   }
   pixelsv1 = vec_mergeh(vczero, pixelsv1);
   pixelsv2 = vec_mergeh(vczero, pixelsv2);
   pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                        (vector unsigned short)pixelsv2);
   pixelssum1 = vec_add(pixelssum1, vcone);
   
POWERPC_PERF_START_COUNT(altivec_put_no_rnd_pixels8_xy2_num, 1); 
   for (i = 0; i < h ; i++) {
     int rightside = ((unsigned long)block & 0x0000000F);
     blockv = vec_ld(0, block);

     temp1 = vec_ld(line_size, pixels);
     temp2 = vec_ld(line_size + 16, pixels);
     pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
     if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F)
     {
       pixelsv2 = temp2;
     }
     else
     {
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
     
     if (rightside)
     {
       blockv = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
     }
     else
     {
       blockv = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
     }
     
     vec_st(blockv, 0, block);
     
     block += line_size;
     pixels += line_size;
   }
   
POWERPC_PERF_STOP_COUNT(altivec_put_no_rnd_pixels8_xy2_num, 1);
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 16) == 0) */
void put_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_put_pixels16_xy2_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int j;
POWERPC_PERF_START_COUNT(altivec_put_pixels16_xy2_num, 1);
      for (j = 0; j < 4; j++) {
      int i;
      const uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
      const uint32_t b =
        (((const struct unaligned_32 *) (pixels + 1))->l);
      uint32_t l0 =
        (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
      uint32_t h0 =
        ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
      uint32_t l1, h1;
      pixels += line_size;
      for (i = 0; i < h; i += 2) {
        uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
        uint32_t b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l1 = (a & 0x03030303UL) + (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
        a = (((const struct unaligned_32 *) (pixels))->l);
        b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
      } pixels += 4 - line_size * (h + 1);
      block += 4 - line_size * h;
    }

POWERPC_PERF_STOP_COUNT(altivec_put_pixels16_xy2_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
   register int i;
   register vector unsigned char
     pixelsv1, pixelsv2, pixelsv3, pixelsv4;
   register vector unsigned char
     blockv, temp1, temp2;
   register vector unsigned short
     pixelssum1, pixelssum2, temp3,
     pixelssum3, pixelssum4, temp4;
   register const_vector unsigned char vczero = (const_vector unsigned char)vec_splat_u8(0);
   register const_vector unsigned short vctwo = (const_vector unsigned short)vec_splat_u16(2);

POWERPC_PERF_START_COUNT(altivec_put_pixels16_xy2_num, 1);
 
   temp1 = vec_ld(0, pixels);
   temp2 = vec_ld(16, pixels);
   pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
   if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F)
   {
     pixelsv2 = temp2;
   }
   else
   {
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
     if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F)
     {
       pixelsv2 = temp2;
     }
     else
     {
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
   
POWERPC_PERF_STOP_COUNT(altivec_put_pixels16_xy2_num, 1);
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

/* next one assumes that ((line_size % 16) == 0) */
void put_no_rnd_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_put_no_rnd_pixels16_xy2_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
    int j;
POWERPC_PERF_START_COUNT(altivec_put_no_rnd_pixels16_xy2_num, 1);
      for (j = 0; j < 4; j++) {
      int i;
      const uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
      const uint32_t b =
        (((const struct unaligned_32 *) (pixels + 1))->l);
      uint32_t l0 =
        (a & 0x03030303UL) + (b & 0x03030303UL) + 0x01010101UL;
      uint32_t h0 =
        ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
      uint32_t l1, h1;
      pixels += line_size;
      for (i = 0; i < h; i += 2) {
        uint32_t a = (((const struct unaligned_32 *) (pixels))->l);
        uint32_t b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l1 = (a & 0x03030303UL) + (b & 0x03030303UL);
        h1 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
        a = (((const struct unaligned_32 *) (pixels))->l);
        b = (((const struct unaligned_32 *) (pixels + 1))->l);
        l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x01010101UL;
        h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
        *((uint32_t *) block) =
          h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL);
        pixels += line_size;
        block += line_size;
      } pixels += 4 - line_size * (h + 1);
      block += 4 - line_size * h;
    }

POWERPC_PERF_STOP_COUNT(altivec_put_no_rnd_pixels16_xy2_num, 1);

#else /* ALTIVEC_USE_REFERENCE_C_CODE */
   register int i;
   register vector unsigned char
     pixelsv1, pixelsv2, pixelsv3, pixelsv4;
   register vector unsigned char
     blockv, temp1, temp2;
   register vector unsigned short
     pixelssum1, pixelssum2, temp3,
     pixelssum3, pixelssum4, temp4;
   register const_vector unsigned char vczero = (const_vector unsigned char)vec_splat_u8(0);
   register const_vector unsigned short vcone = (const_vector unsigned short)vec_splat_u16(1);
   register const_vector unsigned short vctwo = (const_vector unsigned short)vec_splat_u16(2);

POWERPC_PERF_START_COUNT(altivec_put_no_rnd_pixels16_xy2_num, 1);
 
   temp1 = vec_ld(0, pixels);
   temp2 = vec_ld(16, pixels);
   pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
   if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F)
   {
     pixelsv2 = temp2;
   }
   else
   {
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
     if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F)
     {
       pixelsv2 = temp2;
     }
     else
     {
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
   
POWERPC_PERF_STOP_COUNT(altivec_put_no_rnd_pixels16_xy2_num, 1);
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}

#ifdef CONFIG_DARWIN
int hadamard8_diff8x8_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h){
POWERPC_PERF_DECLARE(altivec_hadamard8_diff8x8_num, 1);
  int sum;
POWERPC_PERF_START_COUNT(altivec_hadamard8_diff8x8_num, 1);
  register const_vector unsigned char vzero = (const_vector unsigned char)vec_splat_u8(0);
  register vector signed short temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
  {
    register const_vector signed short vprod1 = (const_vector signed short)AVV( 1,-1, 1,-1, 1,-1, 1,-1);
    register const_vector signed short vprod2 = (const_vector signed short)AVV( 1, 1,-1,-1, 1, 1,-1,-1);
    register const_vector signed short vprod3 = (const_vector signed short)AVV( 1, 1, 1, 1,-1,-1,-1,-1);
    register const_vector unsigned char perm1 = (const_vector unsigned char)
      AVV(0x02, 0x03, 0x00, 0x01,
       0x06, 0x07, 0x04, 0x05,
       0x0A, 0x0B, 0x08, 0x09,
       0x0E, 0x0F, 0x0C, 0x0D);
    register const_vector unsigned char perm2 = (const_vector unsigned char)
      AVV(0x04, 0x05, 0x06, 0x07,
       0x00, 0x01, 0x02, 0x03,
       0x0C, 0x0D, 0x0E, 0x0F,
       0x08, 0x09, 0x0A, 0x0B);
    register const_vector unsigned char perm3 = (const_vector unsigned char)
      AVV(0x08, 0x09, 0x0A, 0x0B,
       0x0C, 0x0D, 0x0E, 0x0F,
       0x00, 0x01, 0x02, 0x03,
       0x04, 0x05, 0x06, 0x07);

#define ONEITERBUTTERFLY(i, res)					\
    {									\
      register vector unsigned char src1, src2, srcO;		       	\
      register vector unsigned char dst1, dst2, dstO;		       	\
      src1 = vec_ld(stride * i, src);					\
      if ((((stride * i) + (unsigned long)src) & 0x0000000F) > 8)	\
	src2 = vec_ld((stride * i) + 16, src);				\
      srcO = vec_perm(src1, src2, vec_lvsl(stride * i, src));		\
      dst1 = vec_ld(stride * i, dst);					\
      if ((((stride * i) + (unsigned long)dst) & 0x0000000F) > 8)	\
	dst2 = vec_ld((stride * i) + 16, dst);				\
      dstO = vec_perm(dst1, dst2, vec_lvsl(stride * i, dst));		\
      /* promote the unsigned chars to signed shorts */			\
      /* we're in the 8x8 function, we only care for the first 8 */	\
      register vector signed short srcV =			       	\
	(vector signed short)vec_mergeh((vector signed char)vzero, (vector signed char)srcO); \
      register vector signed short dstV =			       	\
	(vector signed short)vec_mergeh((vector signed char)vzero, (vector signed char)dstO); \
      /* substractions inside the first butterfly */			\
      register vector signed short but0 = vec_sub(srcV, dstV);	       	\
      register vector signed short op1 = vec_perm(but0, but0, perm1);  	\
      register vector signed short but1 = vec_mladd(but0, vprod1, op1);	\
      register vector signed short op2 = vec_perm(but1, but1, perm2);  	\
      register vector signed short but2 = vec_mladd(but1, vprod2, op2);	\
      register vector signed short op3 = vec_perm(but2, but2, perm3);  	\
      res = vec_mladd(but2, vprod3, op3);				\
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
POWERPC_PERF_STOP_COUNT(altivec_hadamard8_diff8x8_num, 1);
  return sum;
}

/*
  16x8 works with 16 elements ; it allows to avoid replicating
  loads, and give the compiler more rooms for scheduling.
  It's only used from inside hadamard8_diff16_altivec.
  
  Unfortunately, it seems gcc-3.3 is a bit dumb, and
  the compiled code has a LOT of spill code, it seems
  gcc (unlike xlc) cannot keep everything in registers
  by itself. The following code include hand-made
  registers allocation. It's not clean, but on
  a 7450 the resulting code is much faster (best case
  fall from 700+ cycles to 550).
  
  xlc doesn't add spill code, but it doesn't know how to
  schedule for the 7450, and its code isn't much faster than
  gcc-3.3 on the 7450 (but uses 25% less instructions...)
  
  On the 970, the hand-made RA is still a win (arount 690
  vs. around 780), but xlc goes to around 660 on the
  regular C code...
*/

static int hadamard8_diff16x8_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h) {
  int sum;
  register vector signed short
    temp0 asm ("v0"),
    temp1 asm ("v1"),
    temp2 asm ("v2"),
    temp3 asm ("v3"),
    temp4 asm ("v4"),
    temp5 asm ("v5"),
    temp6 asm ("v6"),
    temp7 asm ("v7");
  register vector signed short
    temp0S asm ("v8"),
    temp1S asm ("v9"),
    temp2S asm ("v10"),
    temp3S asm ("v11"),
    temp4S asm ("v12"),
    temp5S asm ("v13"),
    temp6S asm ("v14"),
    temp7S asm ("v15");
  register const_vector unsigned char vzero asm ("v31")= (const_vector unsigned char)vec_splat_u8(0);
  {
    register const_vector signed short vprod1 asm ("v16")= (const_vector signed short)AVV( 1,-1, 1,-1, 1,-1, 1,-1);
    register const_vector signed short vprod2 asm ("v17")= (const_vector signed short)AVV( 1, 1,-1,-1, 1, 1,-1,-1);
    register const_vector signed short vprod3 asm ("v18")= (const_vector signed short)AVV( 1, 1, 1, 1,-1,-1,-1,-1);
    register const_vector unsigned char perm1 asm ("v19")= (const_vector unsigned char)
      AVV(0x02, 0x03, 0x00, 0x01,
       0x06, 0x07, 0x04, 0x05,
       0x0A, 0x0B, 0x08, 0x09,
       0x0E, 0x0F, 0x0C, 0x0D);
    register const_vector unsigned char perm2 asm ("v20")= (const_vector unsigned char)
      AVV(0x04, 0x05, 0x06, 0x07,
       0x00, 0x01, 0x02, 0x03,
       0x0C, 0x0D, 0x0E, 0x0F,
       0x08, 0x09, 0x0A, 0x0B);
    register const_vector unsigned char perm3 asm ("v21")= (const_vector unsigned char)
      AVV(0x08, 0x09, 0x0A, 0x0B,
       0x0C, 0x0D, 0x0E, 0x0F,
       0x00, 0x01, 0x02, 0x03,
       0x04, 0x05, 0x06, 0x07);

#define ONEITERBUTTERFLY(i, res1, res2)					\
    {									\
      register vector unsigned char src1 asm ("v22"), src2 asm ("v23"); \
      register vector unsigned char dst1 asm ("v24"), dst2 asm ("v25"); \
      src1 = vec_ld(stride * i, src);					\
      src2 = vec_ld((stride * i) + 16, src);				\
      register vector unsigned char srcO asm ("v22") = vec_perm(src1, src2, vec_lvsl(stride * i, src)); \
      dst1 = vec_ld(stride * i, dst);					\
      dst2 = vec_ld((stride * i) + 16, dst);				\
      register vector unsigned char dstO asm ("v23") = vec_perm(dst1, dst2, vec_lvsl(stride * i, dst)); \
      /* promote the unsigned chars to signed shorts */			\
      register vector signed short srcV asm ("v24") =                   \
	(vector signed short)vec_mergeh((vector signed char)vzero, (vector signed char)srcO); \
      register vector signed short dstV asm ("v25") =                   \
	(vector signed short)vec_mergeh((vector signed char)vzero, (vector signed char)dstO); \
      register vector signed short srcW asm ("v26") =                   \
	(vector signed short)vec_mergel((vector signed char)vzero, (vector signed char)srcO); \
      register vector signed short dstW asm ("v27") =                   \
	(vector signed short)vec_mergel((vector signed char)vzero, (vector signed char)dstO); \
      /* substractions inside the first butterfly */			\
      register vector signed short but0 asm ("v28") = vec_sub(srcV, dstV); \
      register vector signed short but0S asm ("v29") = vec_sub(srcW, dstW); \
      register vector signed short op1 asm ("v30") = vec_perm(but0, but0, perm1); \
      register vector signed short but1 asm ("v22") = vec_mladd(but0, vprod1, op1); \
      register vector signed short op1S asm ("v23") = vec_perm(but0S, but0S, perm1); \
      register vector signed short but1S asm ("v24") = vec_mladd(but0S, vprod1, op1S); \
      register vector signed short op2 asm ("v25") = vec_perm(but1, but1, perm2); \
      register vector signed short but2 asm ("v26") = vec_mladd(but1, vprod2, op2); \
      register vector signed short op2S asm ("v27") = vec_perm(but1S, but1S, perm2); \
      register vector signed short but2S asm ("v28") = vec_mladd(but1S, vprod2, op2S); \
      register vector signed short op3 asm ("v29") = vec_perm(but2, but2, perm3); \
      res1 = vec_mladd(but2, vprod3, op3);				\
      register vector signed short op3S asm ("v30") = vec_perm(but2S, but2S, perm3); \
      res2 = vec_mladd(but2S, vprod3, op3S);				\
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

int hadamard8_diff16_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h){
POWERPC_PERF_DECLARE(altivec_hadamard8_diff16_num, 1);
  int score;
POWERPC_PERF_START_COUNT(altivec_hadamard8_diff16_num, 1);
  score = hadamard8_diff16x8_altivec(s, dst, src, stride, 8);
  if (h==16) {
    dst += 8*stride;
    src += 8*stride;
    score += hadamard8_diff16x8_altivec(s, dst, src, stride, 8);
  }
POWERPC_PERF_STOP_COUNT(altivec_hadamard8_diff16_num, 1);
  return score;
}
#endif //CONFIG_DARWIN

int has_altivec(void)
{
#ifdef __AMIGAOS4__
	ULONG result = 0;
	extern struct ExecIFace *IExec;

	IExec->GetCPUInfoTags(GCIT_VectorUnit, &result, TAG_DONE);
	if (result == VECTORTYPE_ALTIVEC) return 1;
	return 0;
#else /* __AMIGAOS4__ */

#ifdef CONFIG_DARWIN
    int sels[2] = {CTL_HW, HW_VECTORUNIT};
    int has_vu = 0;
    size_t len = sizeof(has_vu);
    int err;

    err = sysctl(sels, 2, &has_vu, &len, NULL, 0);

    if (err == 0) return (has_vu != 0);
#else /* CONFIG_DARWIN */
/* no Darwin, do it the brute-force way */
/* this is borrowed from the libmpeg2 library */
    {
      signal (SIGILL, sigill_handler);
      if (sigsetjmp (jmpbuf, 1)) {
        signal (SIGILL, SIG_DFL);
      } else {
        canjump = 1;
        
        asm volatile ("mtspr 256, %0\n\t"
                      "vand %%v0, %%v0, %%v0"
                      :
                      : "r" (-1));
        
        signal (SIGILL, SIG_DFL);
        return 1;
      }
    }
#endif /* CONFIG_DARWIN */
    return 0;
#endif /* __AMIGAOS4__ */
}

/* next one assumes that ((line_size % 8) == 0) */
void avg_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h)
{
POWERPC_PERF_DECLARE(altivec_avg_pixels8_xy2_num, 1);
#ifdef ALTIVEC_USE_REFERENCE_C_CODE

    int j;
POWERPC_PERF_START_COUNT(altivec_avg_pixels8_xy2_num, 1);
 for (j = 0; j < 2; j++) {
   int             i;
   const uint32_t  a = (((const struct unaligned_32 *) (pixels))->l);
   const uint32_t  b = (((const struct unaligned_32 *) (pixels + 1))->l);
   uint32_t        l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
   uint32_t        h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
   uint32_t        l1, h1;
   pixels += line_size;
   for (i = 0; i < h; i += 2) {
     uint32_t        a = (((const struct unaligned_32 *) (pixels))->l);
     uint32_t        b = (((const struct unaligned_32 *) (pixels + 1))->l);
     l1 = (a & 0x03030303UL) + (b & 0x03030303UL);
     h1 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
     *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
     pixels += line_size;
     block += line_size;
     a = (((const struct unaligned_32 *) (pixels))->l);
     b = (((const struct unaligned_32 *) (pixels + 1))->l);
     l0 = (a & 0x03030303UL) + (b & 0x03030303UL) + 0x02020202UL;
     h0 = ((a & 0xFCFCFCFCUL) >> 2) + ((b & 0xFCFCFCFCUL) >> 2);
     *((uint32_t *) block) = rnd_avg32(*((uint32_t *) block), h0 + h1 + (((l0 + l1) >> 2) & 0x0F0F0F0FUL));
     pixels += line_size;
     block += line_size;
   } pixels += 4 - line_size * (h + 1);
   block += 4 - line_size * h;
 }
POWERPC_PERF_STOP_COUNT(altivec_avg_pixels8_xy2_num, 1);
#else /* ALTIVEC_USE_REFERENCE_C_CODE */
   register int i;
   register vector unsigned char
     pixelsv1, pixelsv2,
     pixelsavg;
   register vector unsigned char
     blockv, temp1, temp2, blocktemp;
   register vector unsigned short
     pixelssum1, pixelssum2, temp3;
   register const_vector unsigned char vczero = (const_vector unsigned char)vec_splat_u8(0);
   register const_vector unsigned short vctwo = (const_vector unsigned short)vec_splat_u16(2);
   
   temp1 = vec_ld(0, pixels);
   temp2 = vec_ld(16, pixels);
   pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(0, pixels));
   if ((((unsigned long)pixels) & 0x0000000F) ==  0x0000000F)
   {
     pixelsv2 = temp2;
   }
   else
   {
     pixelsv2 = vec_perm(temp1, temp2, vec_lvsl(1, pixels));
   }
   pixelsv1 = vec_mergeh(vczero, pixelsv1);
   pixelsv2 = vec_mergeh(vczero, pixelsv2);
   pixelssum1 = vec_add((vector unsigned short)pixelsv1,
                        (vector unsigned short)pixelsv2);
   pixelssum1 = vec_add(pixelssum1, vctwo);
   
POWERPC_PERF_START_COUNT(altivec_avg_pixels8_xy2_num, 1); 
   for (i = 0; i < h ; i++) {
     int rightside = ((unsigned long)block & 0x0000000F);
     blockv = vec_ld(0, block);

     temp1 = vec_ld(line_size, pixels);
     temp2 = vec_ld(line_size + 16, pixels);
     pixelsv1 = vec_perm(temp1, temp2, vec_lvsl(line_size, pixels));
     if (((((unsigned long)pixels) + line_size) & 0x0000000F) ==  0x0000000F)
     {
       pixelsv2 = temp2;
     }
     else
     {
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
     
     if (rightside)
     {
       blocktemp = vec_perm(blockv, pixelsavg, vcprm(0, 1, s0, s1));
     }
     else
     {
       blocktemp = vec_perm(blockv, pixelsavg, vcprm(s0, s1, 2, 3));
     }
     
     blockv = vec_avg(blocktemp, blockv);
     vec_st(blockv, 0, block);
     
     block += line_size;
     pixels += line_size;
   }
   
POWERPC_PERF_STOP_COUNT(altivec_avg_pixels8_xy2_num, 1);
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
}
