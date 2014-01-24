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

function ff_add_pixels_clamped_armv6, export=1
        push            {r4-r8,lr}
        mov             r3,  #8
1:
        ldm             r0!, {r4,r5,r12,lr}
        ldrd            r6,  r7,  [r1]
        pkhbt           r8,  r4,  r5,  lsl #16
        pkhtb           r5,  r5,  r4,  asr #16
        pkhbt           r4,  r12, lr,  lsl #16
        pkhtb           lr,  lr,  r12, asr #16
        pld             [r1, r2]
        uxtab16         r8,  r8,  r6
        uxtab16         r5,  r5,  r6,  ror #8
        uxtab16         r4,  r4,  r7
        uxtab16         lr,  lr,  r7,  ror #8
        usat16          r8,  #8,  r8
        usat16          r5,  #8,  r5
        usat16          r4,  #8,  r4
        usat16          lr,  #8,  lr
        orr             r6,  r8,  r5,  lsl #8
        orr             r7,  r4,  lr,  lsl #8
        subs            r3,  r3,  #1
        strd_post       r6,  r7,  r1,  r2
        bgt             1b
        pop             {r4-r8,pc}
endfunc
