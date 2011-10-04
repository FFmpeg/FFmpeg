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
