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
 *
 */

/*
 * NOTE: This code is based on GPL code from the libmpeg2 project.  The
 * author, Michel Lespinasses, has given explicit permission to release
 * under LGPL as part of ffmpeg.
 *
 */

/*
 * FFMpeg integration by Dieter Shirley
 *
 * This file is a direct copy of the altivec idct module from the libmpeg2
 * project.  I've deleted all of the libmpeg2 specific code, renamed the functions and
 * re-ordered the function parameters.  The only change to the IDCT function
 * itself was to factor out the partial transposition, and to perform a full
 * transpose at the end of the function.
 */


#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>
#include "../dsputil.h"

#include "gcc_fixes.h"

#include "dsputil_altivec.h"

#define vector_s16_t vector signed short
#define const_vector_s16_t const_vector signed short
#define vector_u16_t vector unsigned short
#define vector_s8_t vector signed char
#define vector_u8_t vector unsigned char
#define vector_s32_t vector signed int
#define vector_u32_t vector unsigned int

#define IDCT_HALF                                       \
    /* 1st stage */                                     \
    t1 = vec_mradds (a1, vx7, vx1 );                    \
    t8 = vec_mradds (a1, vx1, vec_subs (zero, vx7));    \
    t7 = vec_mradds (a2, vx5, vx3);                     \
    t3 = vec_mradds (ma2, vx3, vx5);                    \
                                                        \
    /* 2nd stage */                                     \
    t5 = vec_adds (vx0, vx4);                           \
    t0 = vec_subs (vx0, vx4);                           \
    t2 = vec_mradds (a0, vx6, vx2);                     \
    t4 = vec_mradds (a0, vx2, vec_subs (zero, vx6));    \
    t6 = vec_adds (t8, t3);                             \
    t3 = vec_subs (t8, t3);                             \
    t8 = vec_subs (t1, t7);                             \
    t1 = vec_adds (t1, t7);                             \
                                                        \
    /* 3rd stage */                                     \
    t7 = vec_adds (t5, t2);                             \
    t2 = vec_subs (t5, t2);                             \
    t5 = vec_adds (t0, t4);                             \
    t0 = vec_subs (t0, t4);                             \
    t4 = vec_subs (t8, t3);                             \
    t3 = vec_adds (t8, t3);                             \
                                                        \
    /* 4th stage */                                     \
    vy0 = vec_adds (t7, t1);                            \
    vy7 = vec_subs (t7, t1);                            \
    vy1 = vec_mradds (c4, t3, t5);                      \
    vy6 = vec_mradds (mc4, t3, t5);                     \
    vy2 = vec_mradds (c4, t4, t0);                      \
    vy5 = vec_mradds (mc4, t4, t0);                     \
    vy3 = vec_adds (t2, t6);                            \
    vy4 = vec_subs (t2, t6);


#define IDCT                                                            \
    vector_s16_t vx0, vx1, vx2, vx3, vx4, vx5, vx6, vx7;                \
    vector_s16_t vy0, vy1, vy2, vy3, vy4, vy5, vy6, vy7;                \
    vector_s16_t a0, a1, a2, ma2, c4, mc4, zero, bias;                  \
    vector_s16_t t0, t1, t2, t3, t4, t5, t6, t7, t8;                    \
    vector_u16_t shift;                                                 \
                                                                        \
    c4 = vec_splat (constants[0], 0);                                   \
    a0 = vec_splat (constants[0], 1);                                   \
    a1 = vec_splat (constants[0], 2);                                   \
    a2 = vec_splat (constants[0], 3);                                   \
    mc4 = vec_splat (constants[0], 4);                                  \
    ma2 = vec_splat (constants[0], 5);                                  \
    bias = (vector_s16_t)vec_splat ((vector_s32_t)constants[0], 3);     \
                                                                        \
    zero = vec_splat_s16 (0);                                           \
    shift = vec_splat_u16 (4);                                          \
                                                                        \
    vx0 = vec_mradds (vec_sl (block[0], shift), constants[1], zero);    \
    vx1 = vec_mradds (vec_sl (block[1], shift), constants[2], zero);    \
    vx2 = vec_mradds (vec_sl (block[2], shift), constants[3], zero);    \
    vx3 = vec_mradds (vec_sl (block[3], shift), constants[4], zero);    \
    vx4 = vec_mradds (vec_sl (block[4], shift), constants[1], zero);    \
    vx5 = vec_mradds (vec_sl (block[5], shift), constants[4], zero);    \
    vx6 = vec_mradds (vec_sl (block[6], shift), constants[3], zero);    \
    vx7 = vec_mradds (vec_sl (block[7], shift), constants[2], zero);    \
                                                                        \
    IDCT_HALF                                                           \
                                                                        \
    vx0 = vec_mergeh (vy0, vy4);                                        \
    vx1 = vec_mergel (vy0, vy4);                                        \
    vx2 = vec_mergeh (vy1, vy5);                                        \
    vx3 = vec_mergel (vy1, vy5);                                        \
    vx4 = vec_mergeh (vy2, vy6);                                        \
    vx5 = vec_mergel (vy2, vy6);                                        \
    vx6 = vec_mergeh (vy3, vy7);                                        \
    vx7 = vec_mergel (vy3, vy7);                                        \
                                                                        \
    vy0 = vec_mergeh (vx0, vx4);                                        \
    vy1 = vec_mergel (vx0, vx4);                                        \
    vy2 = vec_mergeh (vx1, vx5);                                        \
    vy3 = vec_mergel (vx1, vx5);                                        \
    vy4 = vec_mergeh (vx2, vx6);                                        \
    vy5 = vec_mergel (vx2, vx6);                                        \
    vy6 = vec_mergeh (vx3, vx7);                                        \
    vy7 = vec_mergel (vx3, vx7);                                        \
                                                                        \
    vx0 = vec_adds (vec_mergeh (vy0, vy4), bias);                       \
    vx1 = vec_mergel (vy0, vy4);                                        \
    vx2 = vec_mergeh (vy1, vy5);                                        \
    vx3 = vec_mergel (vy1, vy5);                                        \
    vx4 = vec_mergeh (vy2, vy6);                                        \
    vx5 = vec_mergel (vy2, vy6);                                        \
    vx6 = vec_mergeh (vy3, vy7);                                        \
    vx7 = vec_mergel (vy3, vy7);                                        \
                                                                        \
    IDCT_HALF                                                           \
                                                                        \
    shift = vec_splat_u16 (6);                                          \
    vx0 = vec_sra (vy0, shift);                                         \
    vx1 = vec_sra (vy1, shift);                                         \
    vx2 = vec_sra (vy2, shift);                                         \
    vx3 = vec_sra (vy3, shift);                                         \
    vx4 = vec_sra (vy4, shift);                                         \
    vx5 = vec_sra (vy5, shift);                                         \
    vx6 = vec_sra (vy6, shift);                                         \
    vx7 = vec_sra (vy7, shift);


static const_vector_s16_t constants[5] = {
    (vector_s16_t) AVV(23170, 13573, 6518, 21895, -23170, -21895, 32, 31),
    (vector_s16_t) AVV(16384, 22725, 21407, 19266, 16384, 19266, 21407, 22725),
    (vector_s16_t) AVV(22725, 31521, 29692, 26722, 22725, 26722, 29692, 31521),
    (vector_s16_t) AVV(21407, 29692, 27969, 25172, 21407, 25172, 27969, 29692),
    (vector_s16_t) AVV(19266, 26722, 25172, 22654, 19266, 22654, 25172, 26722)
};

void idct_put_altivec(uint8_t* dest, int stride, vector_s16_t* block)
{
POWERPC_PERF_DECLARE(altivec_idct_put_num, 1);
    vector_u8_t tmp;

#ifdef CONFIG_POWERPC_PERF
POWERPC_PERF_START_COUNT(altivec_idct_put_num, 1);
#endif
    IDCT

#define COPY(dest,src)                                          \
    tmp = vec_packsu (src, src);                                \
    vec_ste ((vector_u32_t)tmp, 0, (unsigned int *)dest);       \
    vec_ste ((vector_u32_t)tmp, 4, (unsigned int *)dest);

    COPY (dest, vx0)    dest += stride;
    COPY (dest, vx1)    dest += stride;
    COPY (dest, vx2)    dest += stride;
    COPY (dest, vx3)    dest += stride;
    COPY (dest, vx4)    dest += stride;
    COPY (dest, vx5)    dest += stride;
    COPY (dest, vx6)    dest += stride;
    COPY (dest, vx7)

POWERPC_PERF_STOP_COUNT(altivec_idct_put_num, 1);
}

void idct_add_altivec(uint8_t* dest, int stride, vector_s16_t* block)
{
POWERPC_PERF_DECLARE(altivec_idct_add_num, 1);
    vector_u8_t tmp;
    vector_s16_t tmp2, tmp3;
    vector_u8_t perm0;
    vector_u8_t perm1;
    vector_u8_t p0, p1, p;

#ifdef CONFIG_POWERPC_PERF
POWERPC_PERF_START_COUNT(altivec_idct_add_num, 1);
#endif

    IDCT

    p0 = vec_lvsl (0, dest);
    p1 = vec_lvsl (stride, dest);
    p = vec_splat_u8 (-1);
    perm0 = vec_mergeh (p, p0);
    perm1 = vec_mergeh (p, p1);

#define ADD(dest,src,perm)                                              \
    /* *(uint64_t *)&tmp = *(uint64_t *)dest; */                        \
    tmp = vec_ld (0, dest);                                             \
    tmp2 = (vector_s16_t)vec_perm (tmp, (vector_u8_t)zero, perm);       \
    tmp3 = vec_adds (tmp2, src);                                        \
    tmp = vec_packsu (tmp3, tmp3);                                      \
    vec_ste ((vector_u32_t)tmp, 0, (unsigned int *)dest);               \
    vec_ste ((vector_u32_t)tmp, 4, (unsigned int *)dest);

    ADD (dest, vx0, perm0)      dest += stride;
    ADD (dest, vx1, perm1)      dest += stride;
    ADD (dest, vx2, perm0)      dest += stride;
    ADD (dest, vx3, perm1)      dest += stride;
    ADD (dest, vx4, perm0)      dest += stride;
    ADD (dest, vx5, perm1)      dest += stride;
    ADD (dest, vx6, perm0)      dest += stride;
    ADD (dest, vx7, perm1)

POWERPC_PERF_STOP_COUNT(altivec_idct_add_num, 1);
}

