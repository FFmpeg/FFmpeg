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

#ifndef AVUTIL_RISCV_TIMER_H
#define AVUTIL_RISCV_TIMER_H

#include "config.h"

#if HAVE_INLINE_ASM
#include <stdint.h>

static inline uint64_t rdcycle64(void)
{
#if (__riscv_xlen >= 64)
    uintptr_t cycles;

    __asm__ volatile ("rdcycle %0" : "=r"(cycles));

#else
    uint64_t cycles;
    uint32_t hi, lo, check;

    __asm__ volatile (
        "1: rdcycleh %0\n"
        "   rdcycle  %1\n"
        "   rdcycleh %2\n"
        "   bne %0, %2, 1b\n" : "=r" (hi), "=r" (lo), "=r" (check));

    cycles = (((uint64_t)hi) << 32) | lo;

#endif
    return cycles;
}

#define AV_READ_TIME rdcycle64

#endif
#endif /* AVUTIL_RISCV_TIMER_H */
