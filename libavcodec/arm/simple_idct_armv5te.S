/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2006 Mans Rullgard <mans@mansr.com>
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

#define W1  22725   /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W2  21407   /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W3  19266   /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W4  16383   /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W5  12873   /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W6  8867    /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define W7  4520    /* cos(i*M_PI/16)*sqrt(2)*(1<<14) + 0.5 */
#define ROW_SHIFT 11
#define COL_SHIFT 20

#define W13 (W1 | (W3 << 16))
#define W26 (W2 | (W6 << 16))
#define W57 (W5 | (W7 << 16))

function idct_row_armv5te
        str    lr, [sp, #-4]!

        ldrd   v1, v2, [a1, #8]
        ldrd   a3, a4, [a1]          /* a3 = row[1:0], a4 = row[3:2] */
        orrs   v1, v1, v2
        itt    eq
        cmpeq  v1, a4
        cmpeq  v1, a3, lsr #16
        beq    row_dc_only

        mov    v1, #(1<<(ROW_SHIFT-1))
        mov    ip, #16384
        sub    ip, ip, #1            /* ip = W4 */
        smlabb v1, ip, a3, v1        /* v1 = W4*row[0]+(1<<(RS-1)) */
        ldr    ip, =W26              /* ip = W2 | (W6 << 16) */
        smultb a2, ip, a4
        smulbb lr, ip, a4
        add    v2, v1, a2
        sub    v3, v1, a2
        sub    v4, v1, lr
        add    v1, v1, lr

        ldr    ip, =W13              /* ip = W1 | (W3 << 16) */
        ldr    lr, =W57              /* lr = W5 | (W7 << 16) */
        smulbt v5, ip, a3
        smultt v6, lr, a4
        smlatt v5, ip, a4, v5
        smultt a2, ip, a3
        smulbt v7, lr, a3
        sub    v6, v6, a2
        smulbt a2, ip, a4
        smultt fp, lr, a3
        sub    v7, v7, a2
        smulbt a2, lr, a4
        ldrd   a3, a4, [a1, #8]     /* a3=row[5:4] a4=row[7:6] */
        sub    fp, fp, a2

        orrs   a2, a3, a4
        beq    1f

        smlabt v5, lr, a3, v5
        smlabt v6, ip, a3, v6
        smlatt v5, lr, a4, v5
        smlabt v6, lr, a4, v6
        smlatt v7, lr, a3, v7
        smlatt fp, ip, a3, fp
        smulbt a2, ip, a4
        smlatt v7, ip, a4, v7
        sub    fp, fp, a2

        ldr    ip, =W26              /* ip = W2 | (W6 << 16) */
        mov    a2, #16384
        sub    a2, a2, #1            /* a2 =  W4 */
        smulbb a2, a2, a3            /* a2 =  W4*row[4] */
        smultb lr, ip, a4            /* lr =  W6*row[6] */
        add    v1, v1, a2            /* v1 += W4*row[4] */
        add    v1, v1, lr            /* v1 += W6*row[6] */
        add    v4, v4, a2            /* v4 += W4*row[4] */
        sub    v4, v4, lr            /* v4 -= W6*row[6] */
        smulbb lr, ip, a4            /* lr =  W2*row[6] */
        sub    v2, v2, a2            /* v2 -= W4*row[4] */
        sub    v2, v2, lr            /* v2 -= W2*row[6] */
        sub    v3, v3, a2            /* v3 -= W4*row[4] */
        add    v3, v3, lr            /* v3 += W2*row[6] */

1:      add    a2, v1, v5
        mov    a3, a2, lsr #11
        bic    a3, a3, #0x1f0000
        sub    a2, v2, v6
        mov    a2, a2, lsr #11
        add    a3, a3, a2, lsl #16
        add    a2, v3, v7
        mov    a4, a2, lsr #11
        bic    a4, a4, #0x1f0000
        add    a2, v4, fp
        mov    a2, a2, lsr #11
        add    a4, a4, a2, lsl #16
        strd   a3, a4, [a1]

        sub    a2, v4, fp
        mov    a3, a2, lsr #11
        bic    a3, a3, #0x1f0000
        sub    a2, v3, v7
        mov    a2, a2, lsr #11
        add    a3, a3, a2, lsl #16
        add    a2, v2, v6
        mov    a4, a2, lsr #11
        bic    a4, a4, #0x1f0000
        sub    a2, v1, v5
        mov    a2, a2, lsr #11
        add    a4, a4, a2, lsl #16
        strd   a3, a4, [a1, #8]

        ldr    pc, [sp], #4

row_dc_only:
        orr    a3, a3, a3, lsl #16
        bic    a3, a3, #0xe000
        mov    a3, a3, lsl #3
        mov    a4, a3
        strd   a3, a4, [a1]
        strd   a3, a4, [a1, #8]

        ldr    pc, [sp], #4
endfunc

        .macro idct_col
        ldr    a4, [a1]              /* a4 = col[1:0] */
        mov    ip, #16384
        sub    ip, ip, #1            /* ip = W4 */
#if 0
        mov    v1, #(1<<(COL_SHIFT-1))
        smlabt v2, ip, a4, v1        /* v2 = W4*col[1] + (1<<(COL_SHIFT-1)) */
        smlabb v1, ip, a4, v1        /* v1 = W4*col[0] + (1<<(COL_SHIFT-1)) */
        ldr    a4, [a1, #(16*4)]
#else
        mov    v1, #((1<<(COL_SHIFT-1))/W4) /* this matches the C version */
        add    v2, v1, a4, asr #16
        rsb    v2, v2, v2, lsl #14
        mov    a4, a4, lsl #16
        add    v1, v1, a4, asr #16
        ldr    a4, [a1, #(16*4)]
        rsb    v1, v1, v1, lsl #14
#endif

        smulbb lr, ip, a4
        smulbt a3, ip, a4
        sub    v3, v1, lr
        sub    v5, v1, lr
        add    v7, v1, lr
        add    v1, v1, lr
        sub    v4, v2, a3
        sub    v6, v2, a3
        add    fp, v2, a3
        ldr    ip, =W26
        ldr    a4, [a1, #(16*2)]
        add    v2, v2, a3

        smulbb lr, ip, a4
        smultb a3, ip, a4
        add    v1, v1, lr
        sub    v7, v7, lr
        add    v3, v3, a3
        sub    v5, v5, a3
        smulbt lr, ip, a4
        smultt a3, ip, a4
        add    v2, v2, lr
        sub    fp, fp, lr
        add    v4, v4, a3
        ldr    a4, [a1, #(16*6)]
        sub    v6, v6, a3

        smultb lr, ip, a4
        smulbb a3, ip, a4
        add    v1, v1, lr
        sub    v7, v7, lr
        sub    v3, v3, a3
        add    v5, v5, a3
        smultt lr, ip, a4
        smulbt a3, ip, a4
        add    v2, v2, lr
        sub    fp, fp, lr
        sub    v4, v4, a3
        add    v6, v6, a3

        stmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp}

        ldr    ip, =W13
        ldr    a4, [a1, #(16*1)]
        ldr    lr, =W57
        smulbb v1, ip, a4
        smultb v3, ip, a4
        smulbb v5, lr, a4
        smultb v7, lr, a4
        smulbt v2, ip, a4
        smultt v4, ip, a4
        smulbt v6, lr, a4
        smultt fp, lr, a4
        rsb    v4, v4, #0
        ldr    a4, [a1, #(16*3)]
        rsb    v3, v3, #0

        smlatb v1, ip, a4, v1
        smlatb v3, lr, a4, v3
        smulbb a3, ip, a4
        smulbb a2, lr, a4
        sub    v5, v5, a3
        sub    v7, v7, a2
        smlatt v2, ip, a4, v2
        smlatt v4, lr, a4, v4
        smulbt a3, ip, a4
        smulbt a2, lr, a4
        sub    v6, v6, a3
        ldr    a4, [a1, #(16*5)]
        sub    fp, fp, a2

        smlabb v1, lr, a4, v1
        smlabb v3, ip, a4, v3
        smlatb v5, lr, a4, v5
        smlatb v7, ip, a4, v7
        smlabt v2, lr, a4, v2
        smlabt v4, ip, a4, v4
        smlatt v6, lr, a4, v6
        ldr    a3, [a1, #(16*7)]
        smlatt fp, ip, a4, fp

        smlatb v1, lr, a3, v1
        smlabb v3, lr, a3, v3
        smlatb v5, ip, a3, v5
        smulbb a4, ip, a3
        smlatt v2, lr, a3, v2
        sub    v7, v7, a4
        smlabt v4, lr, a3, v4
        smulbt a4, ip, a3
        smlatt v6, ip, a3, v6
        sub    fp, fp, a4
        .endm

function idct_col_armv5te
        str    lr, [sp, #-4]!

        idct_col

        ldmfd  sp!, {a3, a4}
        adds   a2, a3, v1
        mov    a2, a2, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        add    ip, a4, v2
        mov    ip, ip, asr #20
        orr    a2, a2, ip, lsl #16
        str    a2, [a1]
        subs   a3, a3, v1
        mov    a2, a3, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        sub    a4, a4, v2
        mov    a4, a4, asr #20
        orr    a2, a2, a4, lsl #16
        ldmfd  sp!, {a3, a4}
        str    a2, [a1, #(16*7)]

        subs   a2, a3, v3
        mov    a2, a2, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        sub    ip, a4, v4
        mov    ip, ip, asr #20
        orr    a2, a2, ip, lsl #16
        str    a2, [a1, #(16*1)]
        adds   a3, a3, v3
        mov    a2, a3, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        add    a4, a4, v4
        mov    a4, a4, asr #20
        orr    a2, a2, a4, lsl #16
        ldmfd  sp!, {a3, a4}
        str    a2, [a1, #(16*6)]

        adds   a2, a3, v5
        mov    a2, a2, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        add    ip, a4, v6
        mov    ip, ip, asr #20
        orr    a2, a2, ip, lsl #16
        str    a2, [a1, #(16*2)]
        subs   a3, a3, v5
        mov    a2, a3, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        sub    a4, a4, v6
        mov    a4, a4, asr #20
        orr    a2, a2, a4, lsl #16
        ldmfd  sp!, {a3, a4}
        str    a2, [a1, #(16*5)]

        adds   a2, a3, v7
        mov    a2, a2, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        add    ip, a4, fp
        mov    ip, ip, asr #20
        orr    a2, a2, ip, lsl #16
        str    a2, [a1, #(16*3)]
        subs   a3, a3, v7
        mov    a2, a3, lsr #20
        it     mi
        orrmi  a2, a2, #0xf000
        sub    a4, a4, fp
        mov    a4, a4, asr #20
        orr    a2, a2, a4, lsl #16
        str    a2, [a1, #(16*4)]

        ldr    pc, [sp], #4
endfunc

.macro  clip   dst, src:vararg
        movs   \dst, \src
        it     mi
        movmi  \dst, #0
        cmp    \dst, #255
        it     gt
        movgt  \dst, #255
.endm

.macro  aclip  dst, src:vararg
        adds   \dst, \src
        it     mi
        movmi  \dst, #0
        cmp    \dst, #255
        it     gt
        movgt  \dst, #255
.endm

function idct_col_put_armv5te
        str    lr, [sp, #-4]!

        idct_col

        ldmfd  sp!, {a3, a4}
        ldr    lr, [sp, #32]
        add    a2, a3, v1
        clip   a2, a2, asr #20
        add    ip, a4, v2
        clip   ip, ip, asr #20
        orr    a2, a2, ip, lsl #8
        sub    a3, a3, v1
        clip   a3, a3, asr #20
        sub    a4, a4, v2
        clip   a4, a4, asr #20
        ldr    v1, [sp, #28]
        strh   a2, [v1]
        add    a2, v1, #2
        str    a2, [sp, #28]
        orr    a2, a3, a4, lsl #8
        rsb    v2, lr, lr, lsl #3
        ldmfd  sp!, {a3, a4}
        strh_pre a2, v2, v1

        sub    a2, a3, v3
        clip   a2, a2, asr #20
        sub    ip, a4, v4
        clip   ip, ip, asr #20
        orr    a2, a2, ip, lsl #8
        strh_pre a2, v1, lr
        add    a3, a3, v3
        clip   a2, a3, asr #20
        add    a4, a4, v4
        clip   a4, a4, asr #20
        orr    a2, a2, a4, lsl #8
        ldmfd  sp!, {a3, a4}
        strh_dpre a2, v2, lr

        add    a2, a3, v5
        clip   a2, a2, asr #20
        add    ip, a4, v6
        clip   ip, ip, asr #20
        orr    a2, a2, ip, lsl #8
        strh_pre a2, v1, lr
        sub    a3, a3, v5
        clip   a2, a3, asr #20
        sub    a4, a4, v6
        clip   a4, a4, asr #20
        orr    a2, a2, a4, lsl #8
        ldmfd  sp!, {a3, a4}
        strh_dpre a2, v2, lr

        add    a2, a3, v7
        clip   a2, a2, asr #20
        add    ip, a4, fp
        clip   ip, ip, asr #20
        orr    a2, a2, ip, lsl #8
        strh   a2, [v1, lr]
        sub    a3, a3, v7
        clip   a2, a3, asr #20
        sub    a4, a4, fp
        clip   a4, a4, asr #20
        orr    a2, a2, a4, lsl #8
        strh_dpre a2, v2, lr

        ldr    pc, [sp], #4
endfunc

function idct_col_add_armv5te
        str    lr, [sp, #-4]!

        idct_col

        ldr    lr, [sp, #36]

        ldmfd  sp!, {a3, a4}
        ldrh   ip, [lr]
        add    a2, a3, v1
        sub    a3, a3, v1
        and    v1, ip, #255
        aclip  a2, v1, a2, asr #20
        add    v1, a4, v2
        mov    v1, v1, asr #20
        aclip  v1, v1, ip, lsr #8
        orr    a2, a2, v1, lsl #8
        ldr    v1, [sp, #32]
        sub    a4, a4, v2
        rsb    v2, v1, v1, lsl #3
        ldrh_pre ip, v2, lr
        strh   a2, [lr]
        and    a2, ip, #255
        aclip  a3, a2, a3, asr #20
        mov    a4, a4, asr #20
        aclip  a4, a4, ip, lsr #8
        add    a2, lr, #2
        str    a2, [sp, #28]
        orr    a2, a3, a4, lsl #8
        strh   a2, [v2]

        ldmfd  sp!, {a3, a4}
        ldrh_pre ip, lr, v1
        sub    a2, a3, v3
        add    a3, a3, v3
        and    v3, ip, #255
        aclip  a2, v3, a2, asr #20
        sub    v3, a4, v4
        mov    v3, v3, asr #20
        aclip  v3, v3, ip, lsr #8
        orr    a2, a2, v3, lsl #8
        add    a4, a4, v4
        ldrh_dpre ip, v2, v1
        strh   a2, [lr]
        and    a2, ip, #255
        aclip  a3, a2, a3, asr #20
        mov    a4, a4, asr #20
        aclip  a4, a4, ip, lsr #8
        orr    a2, a3, a4, lsl #8
        strh   a2, [v2]

        ldmfd  sp!, {a3, a4}
        ldrh_pre ip, lr, v1
        add    a2, a3, v5
        sub    a3, a3, v5
        and    v3, ip, #255
        aclip  a2, v3, a2, asr #20
        add    v3, a4, v6
        mov    v3, v3, asr #20
        aclip  v3, v3, ip, lsr #8
        orr    a2, a2, v3, lsl #8
        sub    a4, a4, v6
        ldrh_dpre ip, v2, v1
        strh   a2, [lr]
        and    a2, ip, #255
        aclip  a3, a2, a3, asr #20
        mov    a4, a4, asr #20
        aclip  a4, a4, ip, lsr #8
        orr    a2, a3, a4, lsl #8
        strh   a2, [v2]

        ldmfd  sp!, {a3, a4}
        ldrh_pre ip, lr, v1
        add    a2, a3, v7
        sub    a3, a3, v7
        and    v3, ip, #255
        aclip  a2, v3, a2, asr #20
        add    v3, a4, fp
        mov    v3, v3, asr #20
        aclip  v3, v3, ip, lsr #8
        orr    a2, a2, v3, lsl #8
        sub    a4, a4, fp
        ldrh_dpre ip, v2, v1
        strh   a2, [lr]
        and    a2, ip, #255
        aclip  a3, a2, a3, asr #20
        mov    a4, a4, asr #20
        aclip  a4, a4, ip, lsr #8
        orr    a2, a3, a4, lsl #8
        strh   a2, [v2]

        ldr    pc, [sp], #4
endfunc

function ff_simple_idct_armv5te, export=1
        stmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, lr}

        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te

        sub    a1, a1, #(16*7)

        bl     idct_col_armv5te
        add    a1, a1, #4
        bl     idct_col_armv5te
        add    a1, a1, #4
        bl     idct_col_armv5te
        add    a1, a1, #4
        bl     idct_col_armv5te

        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
endfunc

function ff_simple_idct_add_armv5te, export=1
        stmfd  sp!, {a1, a2, v1, v2, v3, v4, v5, v6, v7, fp, lr}

        mov    a1, a3

        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te

        sub    a1, a1, #(16*7)

        bl     idct_col_add_armv5te
        add    a1, a1, #4
        bl     idct_col_add_armv5te
        add    a1, a1, #4
        bl     idct_col_add_armv5te
        add    a1, a1, #4
        bl     idct_col_add_armv5te

        add    sp, sp, #8
        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
endfunc

function ff_simple_idct_put_armv5te, export=1
        stmfd  sp!, {a1, a2, v1, v2, v3, v4, v5, v6, v7, fp, lr}

        mov    a1, a3

        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te
        add    a1, a1, #16
        bl     idct_row_armv5te

        sub    a1, a1, #(16*7)

        bl     idct_col_put_armv5te
        add    a1, a1, #4
        bl     idct_col_put_armv5te
        add    a1, a1, #4
        bl     idct_col_put_armv5te
        add    a1, a1, #4
        bl     idct_col_put_armv5te

        add    sp, sp, #8
        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
endfunc
