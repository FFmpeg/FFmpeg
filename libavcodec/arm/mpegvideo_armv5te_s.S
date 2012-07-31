/*
 * Optimization of some functions from mpegvideo.c for armv5te
 * Copyright (c) 2007 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "config.h"
#include "libavutil/arm/asm.S"

/*
 * Special optimized version of dct_unquantize_h263_helper_c, it
 * requires the block to be at least 8 bytes aligned, and may process
 * more elements than requested.  But it is guaranteed to never
 * process more than 64 elements provided that count argument is <= 64,
 * so it is safe. This function is optimized for a common distribution
 * of values for nCoeffs (they are mostly multiple of 8 plus one or
 * two extra elements). So this function processes data as 8 elements
 * per loop iteration and contains optional 2 elements processing in
 * the end.
 *
 * Inner loop should take 6 cycles per element on arm926ej-s (Nokia 770)
 */

.macro  dequant_t       dst, src, mul, add, tmp
        rsbs            \tmp, ip, \src, asr #16
        it              gt
        addgt           \tmp, \add, #0
        it              lt
        rsblt           \tmp, \add, #0
        it              ne
        smlatbne        \dst, \src, \mul, \tmp
.endm

.macro  dequant_b       dst, src, mul, add, tmp
        rsbs            \tmp, ip, \src, lsl #16
        it              gt
        addgt           \tmp, \add, #0
        it              lt
        rsblt           \tmp, \add, #0
        it              ne
        smlabbne        \dst, \src, \mul, \tmp
.endm

function ff_dct_unquantize_h263_armv5te, export=1
        push            {r4-r9,lr}
        mov             ip, #0
        subs            r3, r3, #2
        ble             2f
        ldrd            r4, r5, [r0, #0]
1:
        ldrd            r6, r7, [r0, #8]

        dequant_t       r9, r4, r1, r2, r9
        dequant_t       lr, r5, r1, r2, lr
        dequant_b       r4, r4, r1, r2, r8
        dequant_b       r5, r5, r1, r2, r8

        strh            r4, [r0], #2
        strh            r9, [r0], #2
        strh            r5, [r0], #2
        strh            lr, [r0], #2

        dequant_t       r9, r6, r1, r2, r9
        dequant_t       lr, r7, r1, r2, lr
        dequant_b       r6, r6, r1, r2, r8
        dequant_b       r7, r7, r1, r2, r8

        strh            r6, [r0], #2
        strh            r9, [r0], #2
        strh            r7, [r0], #2
        strh            lr, [r0], #2

        subs            r3, r3, #8
        it              gt
        ldrdgt          r4, r5, [r0, #0] /* load data early to avoid load/use pipeline stall */
        bgt             1b

        adds            r3, r3, #2
        it              le
        pople           {r4-r9,pc}
2:
        ldrsh           r9, [r0, #0]
        ldrsh           lr, [r0, #2]
        mov             r8, r2
        cmp             r9, #0
        it              lt
        rsblt           r8, r2, #0
        it              ne
        smlabbne        r9, r9, r1, r8
        mov             r8, r2
        cmp             lr, #0
        it              lt
        rsblt           r8, r2, #0
        it              ne
        smlabbne        lr, lr, r1, r8
        strh            r9, [r0], #2
        strh            lr, [r0], #2
        pop             {r4-r9,pc}
endfunc
