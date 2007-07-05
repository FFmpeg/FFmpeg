/*
 * Simple IDCT
 *
 * Copyright (c) 2001 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2007 Mans Rullgard <mans@mansr.com>
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
  a1 = source address
  a3 = row[2,0] <= 2 cycles
  a4 = row[3,1]
  ip = w42      <= 2 cycles

  Output in registers v1--v8
*/
        .macro idct_row shift
        ldr    lr, [pc, #(w46-.-8)]  /* lr = W4 | (W6 << 16) */
        mov    a2, #(1<<(\shift-1))
        smlad  v1, a3, ip, a2
        smlsd  v4, a3, ip, a2
        ldr    ip, [pc, #(w13-.-8)]  /* ip = W1 | (W3 << 16) */
        ldr    v7, [pc, #(w57-.-8)]  /* v7 = W5 | (W7 << 16) */
        smlad  v2, a3, lr, a2
        smlsd  v3, a3, lr, a2

        smuad  v5, a4, ip            /* v5 = B0 = W1*row[1] + W3*row[3] */
        smusdx fp, a4, v7            /* fp = B3 = W7*row[1] - W5*row[3] */
        ldr    lr, [a1, #12]         /* lr = row[7,5] */
        pkhtb  a3, ip, v7, asr #16   /* a4 = W7 | (W3 << 16) */
        pkhbt  a2, ip, v7, lsl #16   /* a2 = W1 | (W5 << 16) */
        smusdx v6, a3, a4            /* v6 = -B1 = W7*row[3] - W3*row[1] */
        smlad  v5, lr, v7, v5        /* B0 += W5*row[5] + W7*row[7] */
        smusdx v7, a4, a2            /* v7 = B2 = W5*row[1] - W1*row[3] */

        ldr    a4, [pc, #(w42n-.-8)] /* a4 = -W4 | (-W2 << 16) */
        smlad  v7, lr, a3, v7        /* B2 += W7*row[5] + W3*row[7] */
        ldr    a3, [a1, #4]          /* a3 = row[6,4] */
        smlsdx fp, lr, ip, fp        /* B3 += W3*row[5] - W1*row[7] */
        ldr    ip, [pc, #(w46-.-8)]  /* ip = W4 | (W6 << 16) */
        smlad  v6, lr, a2, v6        /* B1 -= W1*row[5] + W5*row[7] */

        smlad  v2, a3, a4, v2        /* A1 += -W4*row[4] - W2*row[6] */
        smlsd  v3, a3, a4, v3        /* A2 += -W4*row[4] + W2*row[6] */
        smlad  v1, a3, ip, v1        /* A0 += W4*row[4] + W6*row[6] */
        smlsd  v4, a3, ip, v4        /* A3 += W4*row[4] - W6*row[6] */
        .endm

/*
  Compute partial IDCT of half row.
  shift = left-shift amount
  a3 = row[2,0]
  a4 = row[3,1]
  ip = w42

  Output in registers v1--v8
*/
        .macro idct_row4 shift
        ldr    lr, [pc, #(w46-.-8)]  /* lr = W4 | (W6 << 16) */
        ldr    v7, [pc, #(w57-.-8)]  /* v7 = W5 | (W7 << 16) */
        mov    a2, #(1<<(\shift-1))
        smlad  v1, a3, ip, a2
        smlsd  v4, a3, ip, a2
        ldr    ip, [pc, #(w13-.-8)]  /* ip = W1 | (W3 << 16) */
        smlad  v2, a3, lr, a2
        smlsd  v3, a3, lr, a2
        smusdx fp, a4, v7            /* fp = B3 = W7*row[1] - W5*row[3] */
        smuad  v5, a4, ip            /* v5 = B0 = W1*row[1] + W3*row[3] */
        pkhtb  a3, ip, v7, asr #16   /* a4 = W7 | (W3 << 16) */
        pkhbt  a2, ip, v7, lsl #16   /* a2 = W1 | (W5 << 16) */
        smusdx v6, a3, a4            /* v6 = -B1 = W7*row[3] - W3*row[1] */
        smusdx v7, a4, a2            /* v7 = B2 = W5*row[1] - W1*row[3] */
        .endm

/*
  Compute final part of IDCT single row without shift.
  Input in registers v1--v8
  Output in registers ip, v1--v3, lr, v5--v7
*/
        .macro idct_finish
        add    ip, v1, v5            /* a2 = A0 + B0 */
        sub    lr, v1, v5            /* a3 = A0 - B0 */
        sub    v1, v2, v6            /* a3 = A1 + B1 */
        add    v5, v2, v6            /* a3 = A1 - B1 */
        add    v2, v3, v7            /* a2 = A2 + B2 */
        sub    v6, v3, v7            /* a2 = A2 - B2 */
        add    v3, v4, fp            /* a3 = A3 + B3 */
        sub    v7, v4, fp            /* a3 = A3 - B3 */
        .endm

/*
  Compute final part of IDCT single row.
  shift = right-shift amount
  Input/output in registers v1--v8
*/
        .macro idct_finish_shift shift
        add    a4, v1, v5            /* a4 = A0 + B0 */
        sub    a3, v1, v5            /* a3 = A0 - B0 */
        mov    v1, a4, asr #\shift
        mov    v5, a3, asr #\shift

        sub    a4, v2, v6            /* a4 = A1 + B1 */
        add    a3, v2, v6            /* a3 = A1 - B1 */
        mov    v2, a4, asr #\shift
        mov    v6, a3, asr #\shift

        add    a4, v3, v7            /* a4 = A2 + B2 */
        sub    a3, v3, v7            /* a3 = A2 - B2 */
        mov    v3, a4, asr #\shift
        mov    v7, a3, asr #\shift

        add    a4, v4, fp            /* a4 = A3 + B3 */
        sub    a3, v4, fp            /* a3 = A3 - B3 */
        mov    v4, a4, asr #\shift
        mov    fp, a3, asr #\shift
        .endm

/*
  Compute final part of IDCT single row, saturating results at 8 bits.
  shift = right-shift amount
  Input/output in registers v1--v8
*/
        .macro idct_finish_shift_sat shift
        add    a4, v1, v5            /* a4 = A0 + B0 */
        sub    ip, v1, v5            /* ip = A0 - B0 */
        usat   v1, #8, a4, asr #\shift
        usat   v5, #8, ip, asr #\shift

        sub    a4, v2, v6            /* a4 = A1 + B1 */
        add    ip, v2, v6            /* ip = A1 - B1 */
        usat   v2, #8, a4, asr #\shift
        usat   v6, #8, ip, asr #\shift

        add    a4, v3, v7            /* a4 = A2 + B2 */
        sub    ip, v3, v7            /* ip = A2 - B2 */
        usat   v3, #8, a4, asr #\shift
        usat   v7, #8, ip, asr #\shift

        add    a4, v4, fp            /* a4 = A3 + B3 */
        sub    ip, v4, fp            /* ip = A3 - B3 */
        usat   v4, #8, a4, asr #\shift
        usat   fp, #8, ip, asr #\shift
        .endm

/*
  Compute IDCT of single row, storing as column.
  a1 = source
  a2 = dest
*/
        .align
        .type idct_row_armv6, %function
        .func idct_row_armv6
idct_row_armv6:
        str    lr, [sp, #-4]!

        ldr    lr, [a1, #12]         /* lr = row[7,5] */
        ldr    ip, [a1, #4]          /* ip = row[6,4] */
        ldr    a4, [a1, #8]          /* a4 = row[3,1] */
        ldr    a3, [a1]              /* a3 = row[2,0] */
        orrs   lr, lr, ip
        cmpeq  lr, a4
        cmpeq  lr, a3, lsr #16
        beq    1f
        str    a2, [sp, #-4]!
        ldr    ip, [pc, #(w42-.-8)]  /* ip = W4 | (W2 << 16) */
        cmp    lr, #0
        beq    2f

        idct_row   ROW_SHIFT
        b      3f

2:      idct_row4  ROW_SHIFT

3:      ldr    a2, [sp], #4
        idct_finish_shift ROW_SHIFT

        strh   v1, [a2]
        strh   v2, [a2, #(16*2)]
        strh   v3, [a2, #(16*4)]
        strh   v4, [a2, #(16*6)]
        strh   fp, [a2, #(16*1)]
        strh   v7, [a2, #(16*3)]
        strh   v6, [a2, #(16*5)]
        strh   v5, [a2, #(16*7)]

        ldr    pc, [sp], #4

1:      mov    a3, a3, lsl #3
        strh   a3, [a2]
        strh   a3, [a2, #(16*2)]
        strh   a3, [a2, #(16*4)]
        strh   a3, [a2, #(16*6)]
        strh   a3, [a2, #(16*1)]
        strh   a3, [a2, #(16*3)]
        strh   a3, [a2, #(16*5)]
        strh   a3, [a2, #(16*7)]
        ldr    pc, [sp], #4
        .endfunc

/*
  Compute IDCT of single column, read as row.
  a1 = source
  a2 = dest
*/
        .align
        .type idct_col_armv6, %function
        .func idct_col_armv6
idct_col_armv6:
        stmfd  sp!, {a2, lr}

        ldr    a3, [a1]              /* a3 = row[2,0] */
        ldr    ip, [pc, #(w42-.-8)]  /* ip = W4 | (W2 << 16) */
        ldr    a4, [a1, #8]          /* a4 = row[3,1] */
        idct_row COL_SHIFT
        ldr    a2, [sp], #4
        idct_finish_shift COL_SHIFT

        strh   v1, [a2]
        strh   v2, [a2, #(16*1)]
        strh   v3, [a2, #(16*2)]
        strh   v4, [a2, #(16*3)]
        strh   fp, [a2, #(16*4)]
        strh   v7, [a2, #(16*5)]
        strh   v6, [a2, #(16*6)]
        strh   v5, [a2, #(16*7)]

        ldr    pc, [sp], #4
        .endfunc

/*
  Compute IDCT of single column, read as row, store saturated 8-bit.
  a1 = source
  a2 = dest
  a3 = line size
*/
        .align
        .type idct_col_put_armv6, %function
        .func idct_col_put_armv6
idct_col_put_armv6:
        stmfd  sp!, {a2, a3, lr}

        ldr    a3, [a1]              /* a3 = row[2,0] */
        ldr    ip, [pc, #(w42-.-8)]  /* ip = W4 | (W2 << 16) */
        ldr    a4, [a1, #8]          /* a4 = row[3,1] */
        idct_row COL_SHIFT
        ldmfd  sp!, {a2, a3}
        idct_finish_shift_sat COL_SHIFT

        strb   v1, [a2], a3
        strb   v2, [a2], a3
        strb   v3, [a2], a3
        strb   v4, [a2], a3
        strb   fp, [a2], a3
        strb   v7, [a2], a3
        strb   v6, [a2], a3
        strb   v5, [a2], a3

        sub    a2, a2, a3, lsl #3

        ldr    pc, [sp], #4
        .endfunc

/*
  Compute IDCT of single column, read as row, add/store saturated 8-bit.
  a1 = source
  a2 = dest
  a3 = line size
*/
        .align
        .type idct_col_add_armv6, %function
        .func idct_col_add_armv6
idct_col_add_armv6:
        stmfd  sp!, {a2, a3, lr}

        ldr    a3, [a1]              /* a3 = row[2,0] */
        ldr    ip, [pc, #(w42-.-8)]  /* ip = W4 | (W2 << 16) */
        ldr    a4, [a1, #8]          /* a4 = row[3,1] */
        idct_row COL_SHIFT
        ldmfd  sp!, {a2, a3}
        idct_finish

        ldrb   a4, [a2]
        ldrb   v4, [a2, a3]
        ldrb   fp, [a2, a3, lsl #2]
        add    ip, a4, ip, asr #COL_SHIFT
        usat   ip, #8, ip
        add    v1, v4, v1, asr #COL_SHIFT
        strb   ip, [a2], a3
        ldrb   ip, [a2, a3]
        usat   v1, #8, v1
        ldrb   fp, [a2, a3, lsl #2]
        add    v2, ip, v2, asr #COL_SHIFT
        usat   v2, #8, v2
        strb   v1, [a2], a3
        ldrb   a4, [a2, a3]
        ldrb   ip, [a2, a3, lsl #2]
        strb   v2, [a2], a3
        ldrb   v4, [a2, a3]
        ldrb   v1, [a2, a3, lsl #2]
        add    v3, a4, v3, asr #COL_SHIFT
        usat   v3, #8, v3
        add    v7, v4, v7, asr #COL_SHIFT
        usat   v7, #8, v7
        add    v6, fp, v6, asr #COL_SHIFT
        usat   v6, #8, v6
        add    v5, ip, v5, asr #COL_SHIFT
        usat   v5, #8, v5
        add    lr, v1, lr, asr #COL_SHIFT
        usat   lr, #8, lr
        strb   v3, [a2], a3
        strb   v7, [a2], a3
        strb   v6, [a2], a3
        strb   v5, [a2], a3
        strb   lr, [a2], a3

        sub    a2, a2, a3, lsl #3

        ldr    pc, [sp], #4
        .endfunc

/*
  Compute 8 IDCT row transforms.
  func = IDCT row->col function
  width = width of columns in bytes
*/
        .macro idct_rows func width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func
        sub    a1, a1, #(16*5)
        add    a2, a2, #\width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func
        add    a1, a1, #(16*2)
        add    a2, a2, #\width
        bl     \func

        sub    a1, a1, #(16*7)
        .endm

        .align
        .global ff_simple_idct_armv6
        .type ff_simple_idct_armv6, %function
        .func ff_simple_idct_armv6
/* void ff_simple_idct_armv6(DCTELEM *data); */
ff_simple_idct_armv6:
        stmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, lr}
        sub    sp, sp, #128

        mov    a2, sp
        idct_rows idct_row_armv6, 2
        mov    a2, a1
        mov    a1, sp
        idct_rows idct_col_armv6, 2

        add    sp, sp, #128
        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
        .endfunc

        .align
        .global ff_simple_idct_add_armv6
        .type ff_simple_idct_add_armv6, %function
        .func ff_simple_idct_add_armv6
/* ff_simple_idct_add_armv6(uint8_t *dest, int line_size, DCTELEM *data); */
ff_simple_idct_add_armv6:
        stmfd  sp!, {a1, a2, v1, v2, v3, v4, v5, v6, v7, fp, lr}
        sub    sp, sp, #128

        mov    a1, a3
        mov    a2, sp
        idct_rows idct_row_armv6, 2
        mov    a1, sp
        ldr    a2, [sp, #128]
        ldr    a3, [sp, #(128+4)]
        idct_rows idct_col_add_armv6, 1

        add    sp, sp, #(128+8)
        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
        .endfunc

        .align
        .global ff_simple_idct_put_armv6
        .type ff_simple_idct_put_armv6, %function
        .func ff_simple_idct_put_armv6
/* ff_simple_idct_put_armv6(uint8_t *dest, int line_size, DCTELEM *data); */
ff_simple_idct_put_armv6:
        stmfd  sp!, {a1, a2, v1, v2, v3, v4, v5, v6, v7, fp, lr}
        sub    sp, sp, #128

        mov    a1, a3
        mov    a2, sp
        idct_rows idct_row_armv6, 2
        mov    a1, sp
        ldr    a2, [sp, #128]
        ldr    a3, [sp, #(128+4)]
        idct_rows idct_col_put_armv6, 1

        add    sp, sp, #(128+8)
        ldmfd  sp!, {v1, v2, v3, v4, v5, v6, v7, fp, pc}
        .endfunc
