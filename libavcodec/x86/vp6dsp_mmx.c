/**
 * @file libavcodec/x86/vp6dsp_mmx.c
 * MMX-optimized functions for the VP6 decoder
 *
 * Copyright (C) 2009  Sebastien Lucas <sebastien.lucas@gmail.com>
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

#include "libavutil/x86_cpu.h"
#include "libavcodec/dsputil.h"
#include "dsputil_mmx.h"
#include "vp6dsp_mmx.h"


#define DIAG4_MMX(in1,in2,in3,in4)                                            \
    "movq  "#in1"(%0), %%mm0             \n\t"                                \
    "movq  "#in2"(%0), %%mm1             \n\t"                                \
    "movq       %%mm0, %%mm3             \n\t"                                \
    "movq       %%mm1, %%mm4             \n\t"                                \
    "punpcklbw  %%mm7, %%mm0             \n\t"                                \
    "punpcklbw  %%mm7, %%mm1             \n\t"                                \
    "punpckhbw  %%mm7, %%mm3             \n\t"                                \
    "punpckhbw  %%mm7, %%mm4             \n\t"                                \
    "pmullw     0(%2), %%mm0             \n\t" /* src[x-8 ] * biweight [0] */ \
    "pmullw     8(%2), %%mm1             \n\t" /* src[x   ] * biweight [1] */ \
    "pmullw     0(%2), %%mm3             \n\t" /* src[x-8 ] * biweight [0] */ \
    "pmullw     8(%2), %%mm4             \n\t" /* src[x   ] * biweight [1] */ \
    "paddw      %%mm1, %%mm0             \n\t"                                \
    "paddw      %%mm4, %%mm3             \n\t"                                \
    "movq  "#in3"(%0), %%mm1             \n\t"                                \
    "movq  "#in4"(%0), %%mm2             \n\t"                                \
    "movq       %%mm1, %%mm4             \n\t"                                \
    "movq       %%mm2, %%mm5             \n\t"                                \
    "punpcklbw  %%mm7, %%mm1             \n\t"                                \
    "punpcklbw  %%mm7, %%mm2             \n\t"                                \
    "punpckhbw  %%mm7, %%mm4             \n\t"                                \
    "punpckhbw  %%mm7, %%mm5             \n\t"                                \
    "pmullw    16(%2), %%mm1             \n\t" /* src[x+8 ] * biweight [2] */ \
    "pmullw    24(%2), %%mm2             \n\t" /* src[x+16] * biweight [3] */ \
    "pmullw    16(%2), %%mm4             \n\t" /* src[x+8 ] * biweight [2] */ \
    "pmullw    24(%2), %%mm5             \n\t" /* src[x+16] * biweight [3] */ \
    "paddw      %%mm2, %%mm1             \n\t"                                \
    "paddw      %%mm5, %%mm4             \n\t"                                \
    "paddsw     %%mm1, %%mm0             \n\t"                                \
    "paddsw     %%mm4, %%mm3             \n\t"                                \
    "paddsw     %%mm6, %%mm0             \n\t" /* Add 64 */                   \
    "paddsw     %%mm6, %%mm3             \n\t" /* Add 64 */                   \
    "psraw         $7, %%mm0             \n\t"                                \
    "psraw         $7, %%mm3             \n\t"                                \
    "packuswb   %%mm3, %%mm0             \n\t"                                \
    "movq       %%mm0,  (%1)             \n\t"

void ff_vp6_filter_diag4_mmx(uint8_t *dst, uint8_t *src, int stride,
                             const int16_t *h_weights, const int16_t *v_weights)
{
    uint8_t tmp[8*11], *t = tmp;
    int16_t weights[4*4];
    int i;
    src -= stride;

    for (i=0; i<4*4; i++)
        weights[i] = h_weights[i>>2];

    __asm__ volatile(
    "pxor %%mm7, %%mm7                   \n\t"
    "movq "MANGLE(ff_pw_64)", %%mm6      \n\t"
    "1:                                  \n\t"
    DIAG4_MMX(-1,0,1,2)
    "add  $8, %1                         \n\t"
    "add  %3, %0                         \n\t"
    "decl %4                             \n\t"
    "jnz 1b                              \n\t"
    : "+r"(src), "+r"(t)
    : "r"(weights), "r"((x86_reg)stride), "r"(11)
    : "memory");

    t = tmp + 8;
    for (i=0; i<4*4; i++)
        weights[i] = v_weights[i>>2];

    __asm__ volatile(
    "pxor %%mm7, %%mm7                   \n\t"
    "movq "MANGLE(ff_pw_64)", %%mm6      \n\t"
    "1:                                  \n\t"
    DIAG4_MMX(-8,0,8,16)
    "add  $8, %0                         \n\t"
    "add  %3, %1                         \n\t"
    "decl %4                             \n\t"
    "jnz 1b                              \n\t"
    : "+r"(t), "+r"(dst)
    : "r"(weights), "r"((x86_reg)stride), "r"(8)
    : "memory");
}
