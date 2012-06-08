/*
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

/**
 * @file
 * Contains misc utility macros and inline functions
 */

#ifndef AVUTIL_PPC_UTIL_ALTIVEC_H
#define AVUTIL_PPC_UTIL_ALTIVEC_H

#include <stdint.h>

#include "config.h"

#if HAVE_ALTIVEC_H
#include <altivec.h>
#endif

#include "types_altivec.h"

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

#define vcprm(a,b,c,d) (const vector unsigned char){WORD_ ## a, WORD_ ## b, WORD_ ## c, WORD_ ## d}
#define vcii(a,b,c,d) (const vector float){FLOAT_ ## a, FLOAT_ ## b, FLOAT_ ## c, FLOAT_ ## d}

// vcprmle is used to keep the same index as in the SSE version.
// it's the same as vcprm, with the index inversed
// ('le' is Little Endian)
#define vcprmle(a,b,c,d) vcprm(d,c,b,a)

// used to build inverse/identity vectors (vcii)
// n is _n_egative, p is _p_ositive
#define FLOAT_n -1.
#define FLOAT_p 1.


// Transpose 8x8 matrix of 16-bit elements (in-place)
#define TRANSPOSE8(a,b,c,d,e,f,g,h) \
do { \
    vector signed short A1, B1, C1, D1, E1, F1, G1, H1; \
    vector signed short A2, B2, C2, D2, E2, F2, G2, H2; \
 \
    A1 = vec_mergeh (a, e); \
    B1 = vec_mergel (a, e); \
    C1 = vec_mergeh (b, f); \
    D1 = vec_mergel (b, f); \
    E1 = vec_mergeh (c, g); \
    F1 = vec_mergel (c, g); \
    G1 = vec_mergeh (d, h); \
    H1 = vec_mergel (d, h); \
 \
    A2 = vec_mergeh (A1, E1); \
    B2 = vec_mergel (A1, E1); \
    C2 = vec_mergeh (B1, F1); \
    D2 = vec_mergel (B1, F1); \
    E2 = vec_mergeh (C1, G1); \
    F2 = vec_mergel (C1, G1); \
    G2 = vec_mergeh (D1, H1); \
    H2 = vec_mergel (D1, H1); \
 \
    a = vec_mergeh (A2, E2); \
    b = vec_mergel (A2, E2); \
    c = vec_mergeh (B2, F2); \
    d = vec_mergel (B2, F2); \
    e = vec_mergeh (C2, G2); \
    f = vec_mergel (C2, G2); \
    g = vec_mergeh (D2, H2); \
    h = vec_mergel (D2, H2); \
} while (0)


/** @brief loads unaligned vector @a *src with offset @a offset
    and returns it */
static inline vector unsigned char unaligned_load(int offset, uint8_t *src)
{
    register vector unsigned char first = vec_ld(offset, src);
    register vector unsigned char second = vec_ld(offset+15, src);
    register vector unsigned char mask = vec_lvsl(offset, src);
    return vec_perm(first, second, mask);
}

/**
 * loads vector known misalignment
 * @param perm_vec the align permute vector to combine the two loads from lvsl
 */
static inline vec_u8 load_with_perm_vec(int offset, uint8_t *src, vec_u8 perm_vec)
{
    vec_u8 a = vec_ld(offset, src);
    vec_u8 b = vec_ld(offset+15, src);
    return vec_perm(a, b, perm_vec);
}

#endif /* AVUTIL_PPC_UTIL_ALTIVEC_H */
