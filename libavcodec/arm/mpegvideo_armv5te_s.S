/*
 * Optimization of some functions from mpegvideo.c for armv5te
 * Copyright (c) 2007 Siarhei Siamashka <ssvb@users.sourceforge.net>
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

#include "config.h"
#include "asm.S"

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
function ff_dct_unquantize_h263_armv5te, export=1
        push            {r4-r9,lr}
        mov             ip, #0
        subs            r3, r3, #2
        ble             2f
        ldrd            r4, [r0, #0]
1:
        ldrd            r6, [r0, #8]

        rsbs            r9, ip, r4, asr #16
        addgt           r9, r2, #0
        rsblt           r9, r2, #0
        smlatbne        r9, r4, r1, r9

        rsbs            lr, ip, r5, asr #16
        addgt           lr, r2, #0
        rsblt           lr, r2, #0
        smlatbne        lr, r5, r1, lr

        rsbs            r8, ip, r4, asl #16
        addgt           r8, r2, #0
        rsblt           r8, r2, #0
        smlabbne        r4, r4, r1, r8

        rsbs            r8, ip, r5, asl #16
        addgt           r8, r2, #0
        rsblt           r8, r2, #0
        smlabbne        r5, r5, r1, r8

        strh            r4, [r0], #2
        strh            r9, [r0], #2
        strh            r5, [r0], #2
        strh            lr, [r0], #2

        rsbs            r9, ip, r6, asr #16
        addgt           r9, r2, #0
        rsblt           r9, r2, #0
        smlatbne        r9, r6, r1, r9

        rsbs            lr, ip, r7, asr #16
        addgt           lr, r2, #0
        rsblt           lr, r2, #0
        smlatbne        lr, r7, r1, lr

        rsbs            r8, ip, r6, asl #16
        addgt           r8, r2, #0
        rsblt           r8, r2, #0
        smlabbne        r6, r6, r1, r8

        rsbs            r8, ip, r7, asl #16
        addgt           r8, r2, #0
        rsblt           r8, r2, #0
        smlabbne        r7, r7, r1, r8

        strh            r6, [r0], #2
        strh            r9, [r0], #2
        strh            r7, [r0], #2
        strh            lr, [r0], #2

        subs            r3, r3, #8
        ldrgtd          r4, [r0, #0] /* load data early to avoid load/use pipeline stall */
        bgt             1b

        adds            r3, r3, #2
        pople           {r4-r9,pc}
2:
        ldrsh           r9, [r0, #0]
        ldrsh           lr, [r0, #2]
        mov             r8, r2
        cmp             r9, #0
        rsblt           r8, r2, #0
        smlabbne        r9, r9, r1, r8
        mov             r8, r2
        cmp             lr, #0
        rsblt           r8, r2, #0
        smlabbne        lr, lr, r1, r8
        strh            r9, [r0], #2
        strh            lr, [r0], #2
        pop             {r4-r9,pc}
endfunc
