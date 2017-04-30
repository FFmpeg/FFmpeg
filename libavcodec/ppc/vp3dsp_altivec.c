/*
 * Copyright (C) 2009 David Conrad
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

#include <string.h>

#include "config.h"

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/vp3dsp.h"

#if HAVE_ALTIVEC

static const vec_s16 constants =
    {0, 64277, 60547, 54491, 46341, 36410, 25080, 12785};
#if HAVE_BIGENDIAN
static const vec_u8 interleave_high =
    {0, 1, 16, 17, 4, 5, 20, 21, 8, 9, 24, 25, 12, 13, 28, 29};
#else
static const vec_u8 interleave_high =
    {2, 3, 18, 19, 6, 7, 22, 23, 10, 11, 26, 27, 14, 15, 30, 31};
#endif

#define IDCT_START \
    vec_s16 A, B, C, D, Ad, Bd, Cd, Dd, E, F, G, H;\
    vec_s16 Ed, Gd, Add, Bdd, Fd, Hd;\
    vec_s16 eight = vec_splat_s16(8);\
    vec_u16 four = vec_splat_u16(4);\
\
    vec_s16 C1 = vec_splat(constants, 1);\
    vec_s16 C2 = vec_splat(constants, 2);\
    vec_s16 C3 = vec_splat(constants, 3);\
    vec_s16 C4 = vec_splat(constants, 4);\
    vec_s16 C5 = vec_splat(constants, 5);\
    vec_s16 C6 = vec_splat(constants, 6);\
    vec_s16 C7 = vec_splat(constants, 7);\
\
    vec_s16 b0 = vec_ld(0x00, block);\
    vec_s16 b1 = vec_ld(0x10, block);\
    vec_s16 b2 = vec_ld(0x20, block);\
    vec_s16 b3 = vec_ld(0x30, block);\
    vec_s16 b4 = vec_ld(0x40, block);\
    vec_s16 b5 = vec_ld(0x50, block);\
    vec_s16 b6 = vec_ld(0x60, block);\
    vec_s16 b7 = vec_ld(0x70, block);

// these functions do (a*C)>>16
// things are tricky because a is signed, but C unsigned.
// M15 is used if C fits in 15 bit unsigned (C6,C7)
// M16 is used if C requires 16 bits unsigned
static inline vec_s16 M15(vec_s16 a, vec_s16 C)
{
    return (vec_s16)vec_perm(vec_mule(a,C), vec_mulo(a,C), interleave_high);
}
static inline vec_s16 M16(vec_s16 a, vec_s16 C)
{
    return vec_add(a, M15(a, C));
}

#define IDCT_1D(ADD, SHIFT)\
    A = vec_add(M16(b1, C1), M15(b7, C7));\
    B = vec_sub(M15(b1, C7), M16(b7, C1));\
    C = vec_add(M16(b3, C3), M16(b5, C5));\
    D = vec_sub(M16(b5, C3), M16(b3, C5));\
\
    Ad = M16(vec_sub(A, C), C4);\
    Bd = M16(vec_sub(B, D), C4);\
\
    Cd = vec_add(A, C);\
    Dd = vec_add(B, D);\
\
    E = ADD(M16(vec_add(b0, b4), C4));\
    F = ADD(M16(vec_sub(b0, b4), C4));\
\
    G = vec_add(M16(b2, C2), M15(b6, C6));\
    H = vec_sub(M15(b2, C6), M16(b6, C2));\
\
    Ed = vec_sub(E, G);\
    Gd = vec_add(E, G);\
\
    Add = vec_add(F, Ad);\
    Bdd = vec_sub(Bd, H);\
\
    Fd = vec_sub(F, Ad);\
    Hd = vec_add(Bd, H);\
\
    b0 = SHIFT(vec_add(Gd, Cd));\
    b7 = SHIFT(vec_sub(Gd, Cd));\
\
    b1 = SHIFT(vec_add(Add, Hd));\
    b2 = SHIFT(vec_sub(Add, Hd));\
\
    b3 = SHIFT(vec_add(Ed, Dd));\
    b4 = SHIFT(vec_sub(Ed, Dd));\
\
    b5 = SHIFT(vec_add(Fd, Bdd));\
    b6 = SHIFT(vec_sub(Fd, Bdd));

#define NOP(a) a
#define ADD8(a) vec_add(a, eight)
#define SHIFT4(a) vec_sra(a, four)

static void vp3_idct_put_altivec(uint8_t *dst, ptrdiff_t stride, int16_t block[64])
{
    vec_u8 t;
    IDCT_START

    // pixels are signed; so add 128*16 in addition to the normal 8
    vec_s16 v2048 = vec_sl(vec_splat_s16(1), vec_splat_u16(11));
    eight = vec_add(eight, v2048);

    IDCT_1D(NOP, NOP)
    TRANSPOSE8(b0, b1, b2, b3, b4, b5, b6, b7);
    IDCT_1D(ADD8, SHIFT4)

#define PUT(a)\
    t = vec_packsu(a, a);\
    vec_ste((vec_u32)t, 0, (unsigned int *)dst);\
    vec_ste((vec_u32)t, 4, (unsigned int *)dst);

    PUT(b0)     dst += stride;
    PUT(b1)     dst += stride;
    PUT(b2)     dst += stride;
    PUT(b3)     dst += stride;
    PUT(b4)     dst += stride;
    PUT(b5)     dst += stride;
    PUT(b6)     dst += stride;
    PUT(b7)
    memset(block, 0, sizeof(*block) * 64);
}

static void vp3_idct_add_altivec(uint8_t *dst, ptrdiff_t stride, int16_t block[64])
{
    LOAD_ZERO;
    vec_u8 t, vdst;
    vec_s16 vdst_16;
    vec_u8 vdst_mask = vec_mergeh(vec_splat_u8(-1), vec_lvsl(0, dst));

    IDCT_START

    IDCT_1D(NOP, NOP)
    TRANSPOSE8(b0, b1, b2, b3, b4, b5, b6, b7);
    IDCT_1D(ADD8, SHIFT4)

#if HAVE_BIGENDIAN
#define GET_VDST16\
    vdst = vec_ld(0, dst);\
    vdst_16 = (vec_s16)vec_perm(vdst, zero_u8v, vdst_mask);
#else
#define GET_VDST16\
    vdst = vec_vsx_ld(0,dst);\
    vdst_16 = (vec_s16)vec_mergeh(vdst, zero_u8v);
#endif

#define ADD(a)\
    GET_VDST16;\
    vdst_16 = vec_adds(a, vdst_16);\
    t = vec_packsu(vdst_16, vdst_16);\
    vec_ste((vec_u32)t, 0, (unsigned int *)dst);\
    vec_ste((vec_u32)t, 4, (unsigned int *)dst);

    ADD(b0)     dst += stride;
    ADD(b1)     dst += stride;
    ADD(b2)     dst += stride;
    ADD(b3)     dst += stride;
    ADD(b4)     dst += stride;
    ADD(b5)     dst += stride;
    ADD(b6)     dst += stride;
    ADD(b7)
    memset(block, 0, sizeof(*block) * 64);
}

#endif /* HAVE_ALTIVEC */

av_cold void ff_vp3dsp_init_ppc(VP3DSPContext *c, int flags)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    c->idct_put = vp3_idct_put_altivec;
    c->idct_add = vp3_idct_add_altivec;
#endif
}
