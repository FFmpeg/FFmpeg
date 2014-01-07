@
@ ARMv4 optimized DSP utils
@ Copyright (c) 2004 AGAWA Koji <i (AT) atty (DOT) jp>
@
@ This file is part of Libav.
@
@ Libav is free software; you can redistribute it and/or
@ modify it under the terms of the GNU Lesser General Public
@ License as published by the Free Software Foundation; either
@ version 2.1 of the License, or (at your option) any later version.
@
@ Libav is distributed in the hope that it will be useful,
@ but WITHOUT ANY WARRANTY; without even the implied warranty of
@ MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
@ Lesser General Public License for more details.
@
@ You should have received a copy of the GNU Lesser General Public
@ License along with Libav; if not, write to the Free Software
@ Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
@

#include "config.h"
#include "libavutil/arm/asm.S"

@ void ff_add_pixels_clamped_arm(int16_t *block, uint8_t *dest, int stride)
function ff_add_pixels_clamped_arm, export=1, align=5
        push            {r4-r10}
        mov             r10, #8
1:
        ldr             r4,  [r1]               /* load dest */
        /* block[0] and block[1]*/
        ldrsh           r5,  [r0]
        ldrsh           r7,  [r0, #2]
        and             r6,  r4,  #0xFF
        and             r8,  r4,  #0xFF00
        add             r6,  r6,  r5
        add             r8,  r7,  r8,  lsr #8
        mvn             r5,  r5
        mvn             r7,  r7
        tst             r6,  #0x100
        it              ne
        movne           r6,  r5,  lsr #24
        tst             r8,  #0x100
        it              ne
        movne           r8,  r7,  lsr #24
        mov             r9,  r6
        ldrsh           r5,  [r0, #4]           /* moved form [A] */
        orr             r9,  r9,  r8,  lsl #8
        /* block[2] and block[3] */
        /* [A] */
        ldrsh           r7,  [r0, #6]
        and             r6,  r4,  #0xFF0000
        and             r8,  r4,  #0xFF000000
        add             r6,  r5,  r6,  lsr #16
        add             r8,  r7,  r8,  lsr #24
        mvn             r5,  r5
        mvn             r7,  r7
        tst             r6,  #0x100
        it              ne
        movne           r6,  r5,  lsr #24
        tst             r8,  #0x100
        it              ne
        movne           r8,  r7,  lsr #24
        orr             r9,  r9,  r6,  lsl #16
        ldr             r4,  [r1, #4]           /* moved form [B] */
        orr             r9,  r9,  r8,  lsl #24
        /* store dest */
        ldrsh           r5,  [r0, #8]           /* moved form [C] */
        str             r9,  [r1]

        /* load dest */
        /* [B] */
        /* block[4] and block[5] */
        /* [C] */
        ldrsh           r7,  [r0, #10]
        and             r6,  r4,  #0xFF
        and             r8,  r4,  #0xFF00
        add             r6,  r6,  r5
        add             r8,  r7,  r8,  lsr #8
        mvn             r5,  r5
        mvn             r7,  r7
        tst             r6,  #0x100
        it              ne
        movne           r6,  r5,  lsr #24
        tst             r8,  #0x100
        it              ne
        movne           r8,  r7,  lsr #24
        mov             r9,  r6
        ldrsh           r5,  [r0, #12]          /* moved from [D] */
        orr             r9,  r9,  r8,  lsl #8
        /* block[6] and block[7] */
        /* [D] */
        ldrsh           r7,  [r0, #14]
        and             r6,  r4,  #0xFF0000
        and             r8,  r4,  #0xFF000000
        add             r6,  r5,  r6,  lsr #16
        add             r8,  r7,  r8,  lsr #24
        mvn             r5,  r5
        mvn             r7,  r7
        tst             r6,  #0x100
        it              ne
        movne           r6,  r5,  lsr #24
        tst             r8,  #0x100
        it              ne
        movne           r8,  r7,  lsr #24
        orr             r9,  r9,  r6,  lsl #16
        add             r0,  r0,  #16           /* moved from [E] */
        orr             r9,  r9,  r8,  lsl #24
        subs            r10, r10, #1            /* moved from [F] */
        /* store dest */
        str             r9,  [r1, #4]

        /* [E] */
        /* [F] */
        add             r1,  r1,  r2
        bne             1b

        pop             {r4-r10}
        bx              lr
endfunc
