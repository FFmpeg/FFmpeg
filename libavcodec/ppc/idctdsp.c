/*
 * Copyright (c) 2001 Michel Lespinasse
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

/* NOTE: This code is based on GPL code from the libmpeg2 project.  The
 * author, Michel Lespinasses, has given explicit permission to release
 * under LGPL as part of FFmpeg.
 *
 * FFmpeg integration by Dieter Shirley
 *
 * This file is a direct copy of the AltiVec IDCT module from the libmpeg2
 * project.  I've deleted all of the libmpeg2-specific code, renamed the
 * functions and reordered the function parameters.  The only change to the
 * IDCT function itself was to factor out the partial transposition, and to
 * perform a full transpose at the end of the function. */

#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/ppc/cpu.h"
#include "libavutil/ppc/util_altivec.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/idctdsp.h"

#if HAVE_ALTIVEC

#define IDCT_HALF                                       \
    /* 1st stage */                                     \
    t1 = vec_mradds(a1, vx7, vx1);                      \
    t8 = vec_mradds(a1, vx1, vec_subs(zero, vx7));      \
    t7 = vec_mradds(a2, vx5, vx3);                      \
    t3 = vec_mradds(ma2, vx3, vx5);                     \
                                                        \
    /* 2nd stage */                                     \
    t5 = vec_adds(vx0, vx4);                            \
    t0 = vec_subs(vx0, vx4);                            \
    t2 = vec_mradds(a0, vx6, vx2);                      \
    t4 = vec_mradds(a0, vx2, vec_subs(zero, vx6));      \
    t6 = vec_adds(t8, t3);                              \
    t3 = vec_subs(t8, t3);                              \
    t8 = vec_subs(t1, t7);                              \
    t1 = vec_adds(t1, t7);                              \
                                                        \
    /* 3rd stage */                                     \
    t7 = vec_adds(t5, t2);                              \
    t2 = vec_subs(t5, t2);                              \
    t5 = vec_adds(t0, t4);                              \
    t0 = vec_subs(t0, t4);                              \
    t4 = vec_subs(t8, t3);                              \
    t3 = vec_adds(t8, t3);                              \
                                                        \
    /* 4th stage */                                     \
    vy0 = vec_adds(t7, t1);                             \
    vy7 = vec_subs(t7, t1);                             \
    vy1 = vec_mradds(c4, t3, t5);                       \
    vy6 = vec_mradds(mc4, t3, t5);                      \
    vy2 = vec_mradds(c4, t4, t0);                       \
    vy5 = vec_mradds(mc4, t4, t0);                      \
    vy3 = vec_adds(t2, t6);                             \
    vy4 = vec_subs(t2, t6)

#define IDCT                                                                \
    vec_s16 vy0, vy1, vy2, vy3, vy4, vy5, vy6, vy7;                         \
    vec_s16 t0, t1, t2, t3, t4, t5, t6, t7, t8;                             \
                                                                            \
    vec_s16 c4   = vec_splat(constants[0], 0);                              \
    vec_s16 a0   = vec_splat(constants[0], 1);                              \
    vec_s16 a1   = vec_splat(constants[0], 2);                              \
    vec_s16 a2   = vec_splat(constants[0], 3);                              \
    vec_s16 mc4  = vec_splat(constants[0], 4);                              \
    vec_s16 ma2  = vec_splat(constants[0], 5);                              \
    vec_s16 bias = (vec_s16) vec_splat((vec_s32) constants[0], 3);          \
                                                                            \
    vec_s16 zero  = vec_splat_s16(0);                                       \
    vec_u16 shift = vec_splat_u16(4);                                       \
                                                                            \
    vec_s16 vx0 = vec_mradds(vec_sl(block[0], shift), constants[1], zero);  \
    vec_s16 vx1 = vec_mradds(vec_sl(block[1], shift), constants[2], zero);  \
    vec_s16 vx2 = vec_mradds(vec_sl(block[2], shift), constants[3], zero);  \
    vec_s16 vx3 = vec_mradds(vec_sl(block[3], shift), constants[4], zero);  \
    vec_s16 vx4 = vec_mradds(vec_sl(block[4], shift), constants[1], zero);  \
    vec_s16 vx5 = vec_mradds(vec_sl(block[5], shift), constants[4], zero);  \
    vec_s16 vx6 = vec_mradds(vec_sl(block[6], shift), constants[3], zero);  \
    vec_s16 vx7 = vec_mradds(vec_sl(block[7], shift), constants[2], zero);  \
                                                                            \
    IDCT_HALF;                                                              \
                                                                            \
    vx0 = vec_mergeh(vy0, vy4);                                             \
    vx1 = vec_mergel(vy0, vy4);                                             \
    vx2 = vec_mergeh(vy1, vy5);                                             \
    vx3 = vec_mergel(vy1, vy5);                                             \
    vx4 = vec_mergeh(vy2, vy6);                                             \
    vx5 = vec_mergel(vy2, vy6);                                             \
    vx6 = vec_mergeh(vy3, vy7);                                             \
    vx7 = vec_mergel(vy3, vy7);                                             \
                                                                            \
    vy0 = vec_mergeh(vx0, vx4);                                             \
    vy1 = vec_mergel(vx0, vx4);                                             \
    vy2 = vec_mergeh(vx1, vx5);                                             \
    vy3 = vec_mergel(vx1, vx5);                                             \
    vy4 = vec_mergeh(vx2, vx6);                                             \
    vy5 = vec_mergel(vx2, vx6);                                             \
    vy6 = vec_mergeh(vx3, vx7);                                             \
    vy7 = vec_mergel(vx3, vx7);                                             \
                                                                            \
    vx0 = vec_adds(vec_mergeh(vy0, vy4), bias);                             \
    vx1 = vec_mergel(vy0, vy4);                                             \
    vx2 = vec_mergeh(vy1, vy5);                                             \
    vx3 = vec_mergel(vy1, vy5);                                             \
    vx4 = vec_mergeh(vy2, vy6);                                             \
    vx5 = vec_mergel(vy2, vy6);                                             \
    vx6 = vec_mergeh(vy3, vy7);                                             \
    vx7 = vec_mergel(vy3, vy7);                                             \
                                                                            \
    IDCT_HALF;                                                              \
                                                                            \
    shift = vec_splat_u16(6);                                               \
    vx0 = vec_sra(vy0, shift);                                              \
    vx1 = vec_sra(vy1, shift);                                              \
    vx2 = vec_sra(vy2, shift);                                              \
    vx3 = vec_sra(vy3, shift);                                              \
    vx4 = vec_sra(vy4, shift);                                              \
    vx5 = vec_sra(vy5, shift);                                              \
    vx6 = vec_sra(vy6, shift);                                              \
    vx7 = vec_sra(vy7, shift)

static const vec_s16 constants[5] = {
    { 23170, 13573,  6518, 21895, -23170, -21895,    32,    31 },
    { 16384, 22725, 21407, 19266,  16384,  19266, 21407, 22725 },
    { 22725, 31521, 29692, 26722,  22725,  26722, 29692, 31521 },
    { 21407, 29692, 27969, 25172,  21407,  25172, 27969, 29692 },
    { 19266, 26722, 25172, 22654,  19266,  22654, 25172, 26722 }
};

static void idct_altivec(int16_t *blk)
{
    vec_s16 *block = (vec_s16 *) blk;

    IDCT;

    block[0] = vx0;
    block[1] = vx1;
    block[2] = vx2;
    block[3] = vx3;
    block[4] = vx4;
    block[5] = vx5;
    block[6] = vx6;
    block[7] = vx7;
}

static void idct_put_altivec(uint8_t *dest, ptrdiff_t stride, int16_t *blk)
{
    vec_s16 *block = (vec_s16 *) blk;
    vec_u8 tmp;

    IDCT;

#define COPY(dest, src)                                     \
    tmp = vec_packsu(src, src);                             \
    vec_ste((vec_u32) tmp, 0, (unsigned int *) dest);       \
    vec_ste((vec_u32) tmp, 4, (unsigned int *) dest)

    COPY(dest, vx0);
    dest += stride;
    COPY(dest, vx1);
    dest += stride;
    COPY(dest, vx2);
    dest += stride;
    COPY(dest, vx3);
    dest += stride;
    COPY(dest, vx4);
    dest += stride;
    COPY(dest, vx5);
    dest += stride;
    COPY(dest, vx6);
    dest += stride;
    COPY(dest, vx7);
}

static void idct_add_altivec(uint8_t *dest, ptrdiff_t stride, int16_t *blk)
{
    vec_s16 *block = (vec_s16 *) blk;
    vec_u8 tmp;
    vec_s16 tmp2, tmp3;
    vec_u8 perm0;
    vec_u8 perm1;
    vec_u8 p0, p1, p;

    IDCT;

#if HAVE_BIGENDIAN
    p0    = vec_lvsl(0, dest);
    p1    = vec_lvsl(stride, dest);
    p     = vec_splat_u8(-1);
    perm0 = vec_mergeh(p, p0);
    perm1 = vec_mergeh(p, p1);
#endif

#if HAVE_BIGENDIAN
#define GET_TMP2(dest, prm)                                 \
    tmp  = vec_ld(0, dest);                                 \
    tmp2 = (vec_s16) vec_perm(tmp, (vec_u8) zero, prm);
#else
#define GET_TMP2(dest, prm)                                 \
    tmp  = vec_vsx_ld(0, dest);                             \
    tmp2 = (vec_s16) vec_mergeh(tmp, (vec_u8) zero)
#endif

#define ADD(dest, src, perm)                                \
    GET_TMP2(dest, perm);                                   \
    tmp3 = vec_adds(tmp2, src);                             \
    tmp  = vec_packsu(tmp3, tmp3);                          \
    vec_ste((vec_u32) tmp, 0, (unsigned int *) dest);       \
    vec_ste((vec_u32) tmp, 4, (unsigned int *) dest)

    ADD(dest, vx0, perm0);
    dest += stride;
    ADD(dest, vx1, perm1);
    dest += stride;
    ADD(dest, vx2, perm0);
    dest += stride;
    ADD(dest, vx3, perm1);
    dest += stride;
    ADD(dest, vx4, perm0);
    dest += stride;
    ADD(dest, vx5, perm1);
    dest += stride;
    ADD(dest, vx6, perm0);
    dest += stride;
    ADD(dest, vx7, perm1);
}

#endif /* HAVE_ALTIVEC */

av_cold void ff_idctdsp_init_ppc(IDCTDSPContext *c, AVCodecContext *avctx,
                                 unsigned high_bit_depth)
{
#if HAVE_ALTIVEC
    if (!PPC_ALTIVEC(av_get_cpu_flags()))
        return;

    if (!high_bit_depth && avctx->lowres == 0) {
        if ((avctx->idct_algo == FF_IDCT_AUTO && !(avctx->flags & AV_CODEC_FLAG_BITEXACT)) ||
            (avctx->idct_algo == FF_IDCT_ALTIVEC)) {
            c->idct      = idct_altivec;
            c->idct_add  = idct_add_altivec;
            c->idct_put  = idct_put_altivec;
            c->perm_type = FF_IDCT_PERM_TRANSPOSE;
        }
    }
#endif /* HAVE_ALTIVEC */
}
