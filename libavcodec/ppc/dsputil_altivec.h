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

#ifndef _DSPUTIL_ALTIVEC_
#define _DSPUTIL_ALTIVEC_

#include "dsputil_ppc.h"

#ifdef HAVE_ALTIVEC

extern int sad16_x2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int sad16_y2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int sad16_xy2_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int sad16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int sad8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int pix_norm1_altivec(uint8_t *pix, int line_size);
extern int sse8_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int sse16_altivec(void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h);
extern int pix_sum_altivec(uint8_t * pix, int line_size);
extern void diff_pixels_altivec(DCTELEM* block, const uint8_t* s1, const uint8_t* s2, int stride);
extern void get_pixels_altivec(DCTELEM* block, const uint8_t * pixels, int line_size);

extern void add_bytes_altivec(uint8_t *dst, uint8_t *src, int w);
extern void put_pixels_clamped_altivec(const DCTELEM *block, uint8_t *restrict pixels, int line_size);
extern void put_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);
extern void avg_pixels16_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);
extern void avg_pixels8_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h);
extern void put_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);
extern void put_no_rnd_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);
extern void put_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h);
extern void put_no_rnd_pixels16_xy2_altivec(uint8_t * block, const uint8_t * pixels, int line_size, int h);
extern int hadamard8_diff8x8_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h);
extern int hadamard8_diff16_altivec(/*MpegEncContext*/ void *s, uint8_t *dst, uint8_t *src, int stride, int h);
extern void avg_pixels8_xy2_altivec(uint8_t *block, const uint8_t *pixels, int line_size, int h);

extern void gmc1_altivec(uint8_t *dst, uint8_t *src, int stride, int h, int x16, int y16, int rounder);

extern int has_altivec(void);

// used to build registers permutation vectors (vcprm)
// the 's' are for words in the _s_econd vector
#define WORD_0 0x00,0x01,0x02,0x03
#define WORD_1 0x04,0x05,0x06,0x07
#define WORD_2 0x08,0x09,0x0a,0x0b
#define WORD_3 0x0c,0x0d,0x0e,0x0f
#define WORD_s0 0x10,0x11,0x12,0x13
#define WORD_s1 0x14,0x15,0x16,0x17
#define WORD_s2 0x18,0x19,0x1a,0x1b
#define WORD_s3 0x1c,0x1d,0x1e,0x1f

#ifdef CONFIG_DARWIN
#define vcprm(a,b,c,d) (const vector unsigned char)(WORD_ ## a, WORD_ ## b, WORD_ ## c, WORD_ ## d)
#else
#define vcprm(a,b,c,d) (const vector unsigned char){WORD_ ## a, WORD_ ## b, WORD_ ## c, WORD_ ## d}
#endif

// vcprmle is used to keep the same index as in the SSE version.
// it's the same as vcprm, with the index inversed
// ('le' is Little Endian)
#define vcprmle(a,b,c,d) vcprm(d,c,b,a)

// used to build inverse/identity vectors (vcii)
// n is _n_egative, p is _p_ositive
#define FLOAT_n -1.
#define FLOAT_p 1.


#ifdef CONFIG_DARWIN
#define vcii(a,b,c,d) (const vector float)(FLOAT_ ## a, FLOAT_ ## b, FLOAT_ ## c, FLOAT_ ## d)
#else
#define vcii(a,b,c,d) (const vector float){FLOAT_ ## a, FLOAT_ ## b, FLOAT_ ## c, FLOAT_ ## d}
#endif

#else /* HAVE_ALTIVEC */
#ifdef ALTIVEC_USE_REFERENCE_C_CODE
#error "I can't use ALTIVEC_USE_REFERENCE_C_CODE if I don't use HAVE_ALTIVEC"
#endif /* ALTIVEC_USE_REFERENCE_C_CODE */
#endif /* HAVE_ALTIVEC */

#endif /* _DSPUTIL_ALTIVEC_ */
