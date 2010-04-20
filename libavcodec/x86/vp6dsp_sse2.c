/**
 * @file
 * SSE2-optimized functions for the VP6 decoder
 *
 * Copyright (C) 2009  Zuxy Meng <zuxy.meng@gmail.com>
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
#include "vp6dsp_sse2.h"

#define DIAG4_SSE2(in1,in2,in3,in4)                                           \
    "movq  "#in1"(%0), %%xmm0            \n\t"                                \
    "movq  "#in2"(%0), %%xmm1            \n\t"                                \
    "punpcklbw %%xmm7, %%xmm0            \n\t"                                \
    "punpcklbw %%xmm7, %%xmm1            \n\t"                                \
    "pmullw    %%xmm4, %%xmm0            \n\t" /* src[x-8 ] * biweight [0] */ \
    "pmullw    %%xmm5, %%xmm1            \n\t" /* src[x   ] * biweight [1] */ \
    "paddw     %%xmm1, %%xmm0            \n\t"                                \
    "movq  "#in3"(%0), %%xmm1            \n\t"                                \
    "movq  "#in4"(%0), %%xmm2            \n\t"                                \
    "punpcklbw %%xmm7, %%xmm1            \n\t"                                \
    "punpcklbw %%xmm7, %%xmm2            \n\t"                                \
    "pmullw    %%xmm6, %%xmm1            \n\t" /* src[x+8 ] * biweight [2] */ \
    "pmullw    %%xmm3, %%xmm2            \n\t" /* src[x+16] * biweight [3] */ \
    "paddw     %%xmm2, %%xmm1            \n\t"                                \
    "paddsw     %%xmm1, %%xmm0           \n\t"                                \
    "paddsw "MANGLE(ff_pw_64)", %%xmm0   \n\t" /* Add 64 */                   \
    "psraw         $7, %%xmm0            \n\t"                                \
    "packuswb  %%xmm0, %%xmm0            \n\t"                                \
    "movq      %%xmm0,   (%1)            \n\t"                                \

void ff_vp6_filter_diag4_sse2(uint8_t *dst, uint8_t *src, int stride,
                              const int16_t *h_weights,const int16_t *v_weights)
{
    uint8_t tmp[8*11], *t = tmp;
    src -= stride;

    __asm__ volatile(
    "pxor           %%xmm7, %%xmm7       \n\t"
    "movq               %4, %%xmm3       \n\t"
    "pshuflw    $0, %%xmm3, %%xmm4       \n\t"
    "punpcklqdq     %%xmm4, %%xmm4       \n\t"
    "pshuflw   $85, %%xmm3, %%xmm5       \n\t"
    "punpcklqdq     %%xmm5, %%xmm5       \n\t"
    "pshuflw  $170, %%xmm3, %%xmm6       \n\t"
    "punpcklqdq     %%xmm6, %%xmm6       \n\t"
    "pshuflw  $255, %%xmm3, %%xmm3       \n\t"
    "punpcklqdq     %%xmm3, %%xmm3       \n\t"
    "1:                                  \n\t"
    DIAG4_SSE2(-1,0,1,2)
    "add  $8, %1                         \n\t"
    "add  %2, %0                         \n\t"
    "decl %3                             \n\t"
    "jnz 1b                              \n\t"
    : "+r"(src), "+r"(t)
    : "g"((x86_reg)stride), "r"(11), "m"(*(const int64_t*)h_weights)
    : "memory");

    t = tmp + 8;

    __asm__ volatile(
    "movq               %4, %%xmm3       \n\t"
    "pshuflw    $0, %%xmm3, %%xmm4       \n\t"
    "punpcklqdq     %%xmm4, %%xmm4       \n\t"
    "pshuflw   $85, %%xmm3, %%xmm5       \n\t"
    "punpcklqdq     %%xmm5, %%xmm5       \n\t"
    "pshuflw  $170, %%xmm3, %%xmm6       \n\t"
    "punpcklqdq     %%xmm6, %%xmm6       \n\t"
    "pshuflw  $255, %%xmm3, %%xmm3       \n\t"
    "punpcklqdq     %%xmm3, %%xmm3       \n\t"
    "1:                                  \n\t"
    DIAG4_SSE2(-8,0,8,16)
    "add  $8, %0                         \n\t"
    "add  %2, %1                         \n\t"
    "decl %3                             \n\t"
    "jnz 1b                              \n\t"
    : "+r"(t), "+r"(dst)
    : "g"((x86_reg)stride), "r"(8), "m"(*(const int64_t*)v_weights)
    : "memory");
}
