/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2007 Mans Rullgard <mans@mansr.com>
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

#include "asm.S"

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
#define W42 (W4 | (W2 << 16))
#define W42n (-W4&0xffff | (-W2 << 16))
#define W46 (W4 | (W6 << 16))
#define W57 (W5 | (W7 << 16))

        .text
        .align
w13:    .long W13
w26:    .long W26
w42:    .long W42
w42n:   .long W42n
w46:    .long W46
w57:    .long W57

/*
  Compute partial IDCT of single row.
  shift = left-shift amount
  r0 = source address
  r2 = row[2,0] <= 2 cycles
  r3 = row[3,1]
  ip = w42      <= 2 cycles

  Output in registers r4--r11
*/
        .macro idct_row shift
        ldr    lr, w46               /* lr  = W4 | (W6 << 16) */
        mov    r1, #(1<<(\shift-1))
        smlad  r4, r2, ip, r1
        smlsd  r7, r2, ip, r1
        ldr    ip, w13               /* ip  = W1 | (W3 << 16) */
        ldr    r10,w57               /* r10 = W5 | (W7 << 16) */
        smlad  r5, r2, lr, r1
        smlsd  r6, r2, lr, r1

        smuad  r8, r3, ip            /* r8  =  B0 = W1*row[1] + W3*row[3] */
        smusdx r11,r3, r10           /* r11 =  B3 = W7*row[1] - W5*row[3] */
        ldr    lr, [r0, #12]         /* lr  =  row[7,5] */
        pkhtb  r2, ip, r10,asr #16   /* r3  =  W7 | (W3 << 16) */
        pkhbt  r1, ip, r10,lsl #16   /* r1  =  W1 | (W5 << 16) */
        smusdx r9, r2, r3            /* r9  = -B1 = W7*row[3] - W3*row[1] */
        smlad  r8, lr, r10,r8        /* B0  +=      W5*row[5] + W7*row[7] */
        smusdx r10,r3, r1            /* r10 =  B2 = W5*row[1] - W1*row[3] */

        ldr    r3, w42n              /* r3 =  -W4 | (-W2 << 16) */
        smlad  r10,lr, r2, r10       /* B2 +=  W7*row[5] + W3*row[7] */
        ldr    r2, [r0, #4]          /* r2 =   row[6,4] */
        smlsdx r11,lr, ip, r11       /* B3 +=  W3*row[5] - W1*row[7] */
        ldr    ip, w46               /* ip =   W4 | (W6 << 16) */
        smlad  r9, lr, r1, r9        /* B1 -=  W1*row[5] + W5*row[7] */

        smlad  r5, r2, r3, r5        /* A1 += -W4*row[4] - W2*row[6] */
        smlsd  r6, r2, r3, r6        /* A2 += -W4*row[4] + W2*row[6] */
        smlad  r4, r2, ip, r4        /* A0 +=  W4*row[4] + W6*row[6] */
        smlsd  r7, r2, ip, r7        /* A3 +=  W4*row[4] - W6*row[6] */
        .endm

/*
  Compute partial IDCT of half row.
  shift = left-shift amount
  r2 = row[2,0]
  r3 = row[3,1]
  ip = w42

  Output in registers r4--r11
*/
        .macro idct_row4 shift
        ldr    lr, w46               /* lr =  W4 | (W6 << 16) */
        ldr    r10,w57               /* r10 = W5 | (W7 << 16) */
        mov    r1, #(1<<(\shift-1))
        smlad  r4, r2, ip, r1
        smlsd  r7, r2, ip, r1
        ldr    ip, w13               /* ip =  W1 | (W3 << 16) */
        smlad  r5, r2, lr, r1
        smlsd  r6, r2, lr, r1
        smusdx r11,r3, r10           /* r11 =  B3 = W7*row[1] - W5*row[3] */
        smuad  r8, r3, ip            /* r8  =  B0 = W1*row[1] + W3*row[3] */
        pkhtb  r2, ip, r10,asr #16   /* r3  =  W7 | (W3 << 16) */
        pkhbt  r1, ip, r10,lsl #16   /* r1  =  W1 | (W5 << 16) */
        smusdx r9, r2, r3            /* r9  = -B1 = W7*row[3] - W3*row[1] */
        smusdx r10,r3, r1            /* r10 =  B2 = W5*row[1] - W1*row[3] */
        .endm

/*
  Compute final part of IDCT single row without shift.
  Input in registers r4--r11
  Output in registers ip, r4--r6, lr, r8--r10
*/
        .macro idct_finish
        add    ip, r4, r8            /* r1 = A0 + B0 */
        sub    lr, r4, r8            /* r2 = A0 - B0 */
        sub    r4, r5, r9            /* r2 = A1 + B1 */
        add    r8, r5, r9            /* r2 = A1 - B1 */
        add    r5, r6, r10           /* r1 = A2 + B2 */
        sub    r9, r6, r10           /* r1 = A2 - B2 */
        add    r6, r7, r11           /* r2 = A3 + B3 */
        sub    r10,r7, r11           /* r2 = A3 - B3 */
        .endm

/*
  Compute final part of IDCT single row.
  shift = right-shift amount
  Input/output in registers r4--r11
*/
        .macro idct_finish_shift shift
        add    r3, r4, r8            /* r3 = A0 + B0 */
        sub    r2, r4, r8            /* r2 = A0 - B0 */
        mov    r4, r3, asr #\shift
        mov    r8, r2, asr #\shift

        sub    r3, r5, r9            /* r3 = A1 + B1 */
        add    r2, r5, r9            /* r2 = A1 - B1 */
        mov    r5, r3, asr #\shift
        mov    r9, r2, asr #\shift

        add    r3, r6, r10           /* r3 = A2 + B2 */
        sub    r2, r6, r10           /* r2 = A2 - B2 */
        mov    r6, r3, asr #\shift
        mov    r10,r2, asr #\shift

        add    r3, r7, r11           /* r3 = A3 + B3 */
        sub    r2, r7, r11           /* r2 = A3 - B3 */
        mov    r7, r3, asr #\shift
        mov    r11,r2, asr #\shift
        .endm

/*
  Compute final part of IDCT single row, saturating results at 8 bits.
  shift = right-shift amount
  Input/output in registers r4--r11
*/
        .macro idct_finish_shift_sat shift
        add    r3, r4, r8            /* r3 = A0 + B0 */
        sub    ip, r4, r8            /* ip = A0 - B0 */
        usat   r4, #8, r3, asr #\shift
        usat   r8, #8, ip, asr #\shift

        sub    r3, r5, r9            /* r3 = A1 + B1 */
        add    ip, r5, r9            /* ip = A1 - B1 */
        usat   r5, #8, r3, asr #\shift
        usat   r9, #8, ip, asr #\shift

        add    r3, r6, r10           /* r3 = A2 + B2 */
        sub    ip, r6, r10           /* ip = A2 - B2 */
        usat   r6, #8, r3, asr #\shift
        usat   r10,#8, ip, asr #\shift

        add    r3, r7, r11           /* r3 = A3 + B3 */
        sub    ip, r7, r11           /* ip = A3 - B3 */
        usat   r7, #8, r3, asr #\shift
        usat   r11,#8, ip, asr #\shift
        .endm

/*
  Compute IDCT of single row, storing as column.
  r0 = source
  r1 = dest
*/
function idct_row_armv6
        push   {lr}

        ldr    lr, [r0, #12]         /* lr = row[7,5] */
        ldr    ip, [r0, #4]          /* ip = row[6,4] */
        ldr    r3, [r0, #8]          /* r3 = row[3,1] */
        ldr    r2, [r0]              /* r2 = row[2,0] */
        orrs   lr, lr, ip
        itt    eq
        cmpeq  lr, r3
        cmpeq  lr, r2, lsr #16
        beq    1f
        push   {r1}
        ldr    ip, w42               /* ip = W4 | (W2 << 16) */
        cmp    lr, #0
        beq    2f

        idct_row   ROW_SHIFT
        b      3f

2:      idct_row4  ROW_SHIFT

3:      pop    {r1}
        idct_finish_shift ROW_SHIFT

        strh   r4, [r1]
        strh   r5, [r1, #(16*2)]
        strh   r6, [r1, #(16*4)]
        strh   r7, [r1, #(16*6)]
        strh   r11,[r1, #(16*1)]
        strh   r10,[r1, #(16*3)]
        strh   r9, [r1, #(16*5)]
        strh   r8, [r1, #(16*7)]

        pop    {pc}

1:      mov    r2, r2, lsl #3
        strh   r2, [r1]
        strh   r2, [r1, #(16*2)]
        strh   r2, [r1, #(16*4)]
        strh   r2, [r1, #(16*6)]
        strh   r2, [r1, #(16*1)]
        strh   r2, [r1, #(16*3)]
        strh   r2, [r1, #(16*5)]
        strh   r2, [r1, #(16*7)]
        pop    {pc}
endfunc

/*
  Compute IDCT of single column, read as row.
  r0 = source
  r1 = dest
*/
function idct_col_armv6
        push   {r1, lr}

        ldr    r2, [r0]              /* r2 = row[2,0] */
        ldr    ip, w42               /* ip = W4 | (W2 << 16) */
        ldr    r3, [r0, #8]          /* r3 = row[3,1] */
        idct_row COL_SHIFT
        pop    {r1}
        idct_finish_shift COL_SHIFT

        strh   r4, [r1]
        strh   r5, [r1, #(16*1)]
        strh   r6, [r1, #(16*2)]
        strh   r7, [r1, #(16*3)]
        strh   r11,[r1, #(16*4)]
        strh   r10,[r1, #(16*5)]
        strh   r9, [r1, #(16*6)]
        strh   r8, [r1, #(16*7)]

        pop    {pc}
endfunc

/*
  Compute IDCT of single column, read as row, store saturated 8-bit.
  r0 = source
  r1 = dest
  r2 = line size
*/
function idct_col_put_armv6
        push   {r1, r2, lr}

        ldr    r2, [r0]              /* r2 = row[2,0] */
        ldr    ip, w42               /* ip = W4 | (W2 << 16) */
        ldr    r3, [r0, #8]          /* r3 = row[3,1] */
        idct_row COL_SHIFT
        pop    {r1, r2}
        idct_finish_shift_sat COL_SHIFT

        strb_post r4, r1, r2
        strb_post r5, r1, r2
        strb_post r6, r1, r2
        strb_post r7, r1, r2
        strb_post r11,r1, r2
        strb_post r10,r1, r2
        strb_post r9, r1, r2
        strb_post r8, r1, r2

        sub    r1, r1, r2, lsl #3

        pop    {pc}
endfunc

/*
  Compute IDCT of single column, read as row, add/store saturated 8-bit.
  r0 = source
  r1 = dest
  r2 = line size
*/
function idct_col_add_armv6
        push   {r1, r2, lr}

        ldr    r2, [r0]              /* r2 = row[2,0] */
        ldr    ip, w42               /* ip = W4 | (W2 << 16) */
        ldr    r3, [r0, #8]          /* r3 = row[3,1] */
        idct_row COL_SHIFT
        pop    {r1, r2}
        idct_finish

        ldrb   r3, [r1]
        ldrb   r7, [r1, r2]
        ldrb   r11,[r1, r2, lsl #2]
        add    ip, r3, ip, asr #COL_SHIFT
        usat   ip, #8, ip
        add    r4, r7, r4, asr #COL_SHIFT
        strb_post ip, r1, r2
        ldrb   ip, [r1, r2]
        usat   r4, #8, r4
        ldrb   r11,[r1, r2, lsl #2]
        add    r5, ip, r5, asr #COL_SHIFT
        usat   r5, #8, r5
        strb_post r4, r1, r2
        ldrb   r3, [r1, r2]
        ldrb   ip, [r1, r2, lsl #2]
        strb_post r5, r1, r2
        ldrb   r7, [r1, r2]
        ldrb   r4, [r1, r2, lsl #2]
        add    r6, r3, r6, asr #COL_SHIFT
        usat   r6, #8, r6
        add    r10,r7, r10,asr #COL_SHIFT
        usat   r10,#8, r10
        add    r9, r11,r9, asr #COL_SHIFT
        usat   r9, #8, r9
        add    r8, ip, r8, asr #COL_SHIFT
        usat   r8, #8, r8
        add    lr, r4, lr, asr #COL_SHIFT
        usat   lr, #8, lr
        strb_post r6, r1, r2
        strb_post r10,r1, r2
        strb_post r9, r1, r2
        strb_post r8, r1, r2
        strb_post lr, r1, r2

        sub    r1, r1, r2, lsl #3

        pop    {pc}
endfunc

/*
  Compute 8 IDCT row transforms.
  func = IDCT row->col function
  width = width of columns in bytes
*/
        .macro idct_rows func width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func
        sub    r0, r0, #(16*5)
        add    r1, r1, #\width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func
        add    r0, r0, #(16*2)
        add    r1, r1, #\width
        bl     \func

        sub    r0, r0, #(16*7)
        .endm

/* void ff_simple_idct_armv6(DCTELEM *data); */
function ff_simple_idct_armv6, export=1
        push   {r4-r11, lr}
        sub    sp, sp, #128

        mov    r1, sp
        idct_rows idct_row_armv6, 2
        mov    r1, r0
        mov    r0, sp
        idct_rows idct_col_armv6, 2

        add    sp, sp, #128
        pop    {r4-r11, pc}
endfunc

/* ff_simple_idct_add_armv6(uint8_t *dest, int line_size, DCTELEM *data); */
function ff_simple_idct_add_armv6, export=1
        push   {r0, r1, r4-r11, lr}
        sub    sp, sp, #128

        mov    r0, r2
        mov    r1, sp
        idct_rows idct_row_armv6, 2
        mov    r0, sp
        ldr    r1, [sp, #128]
        ldr    r2, [sp, #(128+4)]
        idct_rows idct_col_add_armv6, 1

        add    sp, sp, #(128+8)
        pop    {r4-r11, pc}
endfunc

/* ff_simple_idct_put_armv6(uint8_t *dest, int line_size, DCTELEM *data); */
function ff_simple_idct_put_armv6, export=1
        push   {r0, r1, r4-r11, lr}
        sub    sp, sp, #128

        mov    r0, r2
        mov    r1, sp
        idct_rows idct_row_armv6, 2
        mov    r0, sp
        ldr    r1, [sp, #128]
        ldr    r2, [sp, #(128+4)]
        idct_rows idct_col_put_armv6, 1

        add    sp, sp, #(128+8)
        pop    {r4-r11, pc}
endfunc
