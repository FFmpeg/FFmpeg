/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVCODEC_ARM_DCA_H
#define AVCODEC_ARM_DCA_H

#include <stdint.h>
#include "config.h"
#include "libavutil/intmath.h"

#if HAVE_ARMV6 && HAVE_INLINE_ASM

#define decode_blockcode decode_blockcode
static inline int decode_blockcode(int code, int levels, int *values)
{
    int v0, v1, v2, v3;

    __asm__ ("smmul   %4, %8, %11           \n"
             "smlabb  %8, %4, %10, %8       \n"
             "smmul   %5, %4, %11           \n"
             "sub     %8, %8, %9, lsr #1    \n"
             "smlabb  %4, %5, %10, %4       \n"
             "smmul   %6, %5, %11           \n"
             "str     %8, %0                \n"
             "sub     %4, %4, %9, lsr #1    \n"
             "smlabb  %5, %6, %10, %5       \n"
             "smmul   %7, %6, %11           \n"
             "str     %4, %1                \n"
             "sub     %5, %5, %9, lsr #1    \n"
             "smlabb  %6, %7, %10, %6       \n"
             "cmp     %7, #0                \n"
             "str     %5, %2                \n"
             "sub     %6, %6, %9, lsr #1    \n"
             "it      eq                    \n"
             "mvneq   %7, #0                \n"
             "str     %6, %3                \n"
             : "=m"(values[0]), "=m"(values[1]),
               "=m"(values[2]), "=m"(values[3]),
               "=&r"(v0), "=&r"(v1), "=&r"(v2), "=&r"(v3),
               "+&r"(code)
             : "r"(levels - 1), "r"(-levels), "r"(ff_inverse[levels])
             : "cc");

    return v3;
}

#endif

#if HAVE_NEON && HAVE_INLINE_ASM && HAVE_ASM_MOD_Y

#define int8x8_fmul_int32 int8x8_fmul_int32
static inline void int8x8_fmul_int32(float *dst, const int8_t *src, int scale)
{
    __asm__ ("vcvt.f32.s32 %2,  %2,  #4         \n"
             "vld1.8       {d0},     [%1,:64]   \n"
             "vmovl.s8     q0,  d0              \n"
             "vmovl.s16    q1,  d1              \n"
             "vmovl.s16    q0,  d0              \n"
             "vcvt.f32.s32 q0,  q0              \n"
             "vcvt.f32.s32 q1,  q1              \n"
             "vmul.f32     q0,  q0,  %y2        \n"
             "vmul.f32     q1,  q1,  %y2        \n"
             "vst1.32      {q0-q1},  [%m0,:128] \n"
             : "=Um"(*(float (*)[8])dst)
             : "r"(src), "x"(scale)
             : "d0", "d1", "d2", "d3");
}

#endif

#endif /* AVCODEC_ARM_DCA_H */
