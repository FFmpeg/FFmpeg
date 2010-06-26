/*
 * Alpha optimized DSP utils
 * Copyright (c) 2002 Falk Hueffner <falk@debian.org>
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

/*
 * These functions are scheduled for pca56. They should work
 * reasonably on ev6, though.
 */

#include "regdef.h"

/* Some nicer register names.  */
#define ta t10
#define tb t11
#define tc t12
#define td AT
/* Danger: these overlap with the argument list and the return value */
#define te a5
#define tf a4
#define tg a3
#define th v0

        .set noat
        .set noreorder
        .arch pca56
        .text

/************************************************************************
 * void put_pixels_axp_asm(uint8_t *block, const uint8_t *pixels,
 *                         int line_size, int h)
 */
        .align 6
        .globl put_pixels_axp_asm
        .ent put_pixels_axp_asm
put_pixels_axp_asm:
        .frame sp, 0, ra
        .prologue 0

        and     a1, 7, t0
        beq     t0, $aligned

        .align 4
$unaligned:
        ldq_u   t0, 0(a1)
        ldq_u   t1, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t2, 0(a1)
        ldq_u   t3, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t4, 0(a1)
        ldq_u   t5, 8(a1)
        addq    a1, a2, a1
        nop

        ldq_u   t6, 0(a1)
        ldq_u   t7, 8(a1)
        extql   t0, a1, t0
        addq    a1, a2, a1

        extqh   t1, a1, t1
        addq    a0, a2, t8
        extql   t2, a1, t2
        addq    t8, a2, t9

        extqh   t3, a1, t3
        addq    t9, a2, ta
        extql   t4, a1, t4
        or      t0, t1, t0

        extqh   t5, a1, t5
        or      t2, t3, t2
        extql   t6, a1, t6
        or      t4, t5, t4

        extqh   t7, a1, t7
        or      t6, t7, t6
        stq     t0, 0(a0)
        stq     t2, 0(t8)

        stq     t4, 0(t9)
        subq    a3, 4, a3
        stq     t6, 0(ta)
        addq    ta, a2, a0

        bne     a3, $unaligned
        ret

        .align 4
$aligned:
        ldq     t0, 0(a1)
        addq    a1, a2, a1
        ldq     t1, 0(a1)
        addq    a1, a2, a1

        ldq     t2, 0(a1)
        addq    a1, a2, a1
        ldq     t3, 0(a1)

        addq    a0, a2, t4
        addq    a1, a2, a1
        addq    t4, a2, t5
        subq    a3, 4, a3

        stq     t0, 0(a0)
        addq    t5, a2, t6
        stq     t1, 0(t4)
        addq    t6, a2, a0

        stq     t2, 0(t5)
        stq     t3, 0(t6)

        bne     a3, $aligned
        ret
        .end put_pixels_axp_asm

/************************************************************************
 * void put_pixels_clamped_mvi_asm(const DCTELEM *block, uint8_t *pixels,
 *                                 int line_size)
 */
        .align 6
        .globl put_pixels_clamped_mvi_asm
        .ent put_pixels_clamped_mvi_asm
put_pixels_clamped_mvi_asm:
        .frame sp, 0, ra
        .prologue 0

        lda     t8, -1
        lda     t9, 8           # loop counter
        zap     t8, 0xaa, t8    # 00ff00ff00ff00ff

        .align 4
1:      ldq     t0,  0(a0)
        ldq     t1,  8(a0)
        ldq     t2, 16(a0)
        ldq     t3, 24(a0)

        maxsw4  t0, zero, t0
        subq    t9, 2, t9
        maxsw4  t1, zero, t1
        lda     a0, 32(a0)

        maxsw4  t2, zero, t2
        addq    a1, a2, ta
        maxsw4  t3, zero, t3
        minsw4  t0, t8, t0

        minsw4  t1, t8, t1
        minsw4  t2, t8, t2
        minsw4  t3, t8, t3
        pkwb    t0, t0

        pkwb    t1, t1
        pkwb    t2, t2
        pkwb    t3, t3
        stl     t0, 0(a1)

        stl     t1, 4(a1)
        addq    ta, a2, a1
        stl     t2, 0(ta)
        stl     t3, 4(ta)

        bne     t9, 1b
        ret
        .end put_pixels_clamped_mvi_asm

/************************************************************************
 * void add_pixels_clamped_mvi_asm(const DCTELEM *block, uint8_t *pixels,
 *                                 int line_size)
 */
        .align 6
        .globl add_pixels_clamped_mvi_asm
        .ent add_pixels_clamped_mvi_asm
add_pixels_clamped_mvi_asm:
        .frame sp, 0, ra
        .prologue 0

        lda     t1, -1
        lda     th, 8
        zap     t1, 0x33, tg
        nop

        srl     tg, 1, t0
        xor     tg, t0, tg      # 0x8000800080008000
        zap     t1, 0xaa, tf    # 0x00ff00ff00ff00ff

        .align 4
1:      ldl     t1, 0(a1)       # pix0 (try to hit cache line soon)
        ldl     t4, 4(a1)       # pix1
        addq    a1, a2, te      # pixels += line_size
        ldq     t0, 0(a0)       # shorts0

        ldl     t7, 0(te)       # pix2 (try to hit cache line soon)
        ldl     ta, 4(te)       # pix3
        ldq     t3, 8(a0)       # shorts1
        ldq     t6, 16(a0)      # shorts2

        ldq     t9, 24(a0)      # shorts3
        unpkbw  t1, t1          # 0 0 (quarter/op no.)
        and     t0, tg, t2      # 0 1
        unpkbw  t4, t4          # 1 0

        bic     t0, tg, t0      # 0 2
        unpkbw  t7, t7          # 2 0
        and     t3, tg, t5      # 1 1
        addq    t0, t1, t0      # 0 3

        xor     t0, t2, t0      # 0 4
        unpkbw  ta, ta          # 3 0
        and     t6, tg, t8      # 2 1
        maxsw4  t0, zero, t0    # 0 5

        bic     t3, tg, t3      # 1 2
        bic     t6, tg, t6      # 2 2
        minsw4  t0, tf, t0      # 0 6
        addq    t3, t4, t3      # 1 3

        pkwb    t0, t0          # 0 7
        xor     t3, t5, t3      # 1 4
        maxsw4  t3, zero, t3    # 1 5
        addq    t6, t7, t6      # 2 3

        xor     t6, t8, t6      # 2 4
        and     t9, tg, tb      # 3 1
        minsw4  t3, tf, t3      # 1 6
        bic     t9, tg, t9      # 3 2

        maxsw4  t6, zero, t6    # 2 5
        addq    t9, ta, t9      # 3 3
        stl     t0, 0(a1)       # 0 8
        minsw4  t6, tf, t6      # 2 6

        xor     t9, tb, t9      # 3 4
        maxsw4  t9, zero, t9    # 3 5
        lda     a0, 32(a0)      # block += 16;
        pkwb    t3, t3          # 1 7

        minsw4  t9, tf, t9      # 3 6
        subq    th, 2, th
        pkwb    t6, t6          # 2 7
        pkwb    t9, t9          # 3 7

        stl     t3, 4(a1)       # 1 8
        addq    te, a2, a1      # pixels += line_size
        stl     t6, 0(te)       # 2 8
        stl     t9, 4(te)       # 3 8

        bne     th, 1b
        ret
        .end add_pixels_clamped_mvi_asm
