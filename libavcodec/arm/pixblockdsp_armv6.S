/*
 * Copyright (c) 2009 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/arm/asm.S"

function ff_get_pixels_armv6, export=1
        pld             [r1, r2]
        push            {r4-r8, lr}
        mov             lr,  #8
1:
        ldrd_post       r4,  r5,  r1,  r2
        subs            lr,  lr,  #1
        uxtb16          r6,  r4
        uxtb16          r4,  r4,  ror #8
        uxtb16          r12, r5
        uxtb16          r8,  r5,  ror #8
        pld             [r1, r2]
        pkhbt           r5,  r6,  r4,  lsl #16
        pkhtb           r6,  r4,  r6,  asr #16
        pkhbt           r7,  r12, r8,  lsl #16
        pkhtb           r12, r8,  r12, asr #16
        stm             r0!, {r5,r6,r7,r12}
        bgt             1b

        pop             {r4-r8, pc}
endfunc

function ff_diff_pixels_armv6, export=1
        pld             [r1, r3]
        pld             [r2, r3]
        push            {r4-r9, lr}
        mov             lr,  #8
1:
        ldrd_post       r4,  r5,  r1,  r3
        ldrd_post       r6,  r7,  r2,  r3
        uxtb16          r8,  r4
        uxtb16          r4,  r4,  ror #8
        uxtb16          r9,  r6
        uxtb16          r6,  r6,  ror #8
        pld             [r1, r3]
        ssub16          r9,  r8,  r9
        ssub16          r6,  r4,  r6
        uxtb16          r8,  r5
        uxtb16          r5,  r5,  ror #8
        pld             [r2, r3]
        pkhbt           r4,  r9,  r6,  lsl #16
        pkhtb           r6,  r6,  r9,  asr #16
        uxtb16          r9,  r7
        uxtb16          r7,  r7,  ror #8
        ssub16          r9,  r8,  r9
        ssub16          r5,  r5,  r7
        subs            lr,  lr,  #1
        pkhbt           r8,  r9,  r5,  lsl #16
        pkhtb           r9,  r5,  r9,  asr #16
        stm             r0!, {r4,r6,r8,r9}
        bgt             1b

        pop             {r4-r9, pc}
endfunc
