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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _DSPUTIL_ALTIVEC_
#define _DSPUTIL_ALTIVEC_

#include "dsputil_ppc.h"

#ifdef HAVE_ALTIVEC

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
