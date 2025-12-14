/*
 * Copyright © 2025 Rémi Denis-Courmont.
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

#ifndef AVCODEC_RISCV_MATHOPS_H
#define AVCODEC_RISCV_MATHOPS_H

#include "config.h"
#include <stdbool.h>
#include "libavutil/attributes_internal.h"
#include "libavutil/riscv/cpu.h"

#if HAVE_RV && !defined(__riscv_zbb)
static inline int median3_c(int a, int b, int c);

static inline av_const int median3_rv(int a, int b, int c)
{
    if (__builtin_expect(ff_rv_zbb_support(), true)) {
        int min2, max2;

        __asm__ (
           ".option push\n"
           ".option arch, +zbb\n"
           "max     %1, %2, %3\n"
           "min     %0, %2, %3\n"
           "min     %1, %4, %1\n"
           "max     %0, %0, %1\n"
           ".option pop\n"
           : "=&r" (min2), "=&r" (max2) : "r" (a), "r" (b), "r" (c));

        return min2;
    }
    return median3_c(a, b, c);
}
#define mid_pred median3_rv
#endif

#endif /* HAVE_RVV */
