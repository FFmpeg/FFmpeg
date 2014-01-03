/*
 * Copyright (c) 2013 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
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

RESULT  .req    a1
BUF     .req    a1
SIZE    .req    a2
PATTERN .req    a3
PTR     .req    a4
DAT0    .req    v1
DAT1    .req    v2
DAT2    .req    v3
DAT3    .req    v4
TMP0    .req    v5
TMP1    .req    v6
TMP2    .req    ip
TMP3    .req    lr

#define PRELOAD_DISTANCE 4

.macro innerloop4
        ldr     DAT0, [PTR], #4
        subs    SIZE, SIZE, #4 @ C flag survives rest of macro
        sub     TMP0, DAT0, PATTERN, lsr #14
        bic     TMP0, TMP0, DAT0
        ands    TMP0, TMP0, PATTERN
.endm

.macro innerloop16  decrement, do_preload
        ldmia   PTR!, {DAT0,DAT1,DAT2,DAT3}
 .ifnc "\do_preload",""
        pld     [PTR, #PRELOAD_DISTANCE*32]
 .endif
 .ifnc "\decrement",""
        subs    SIZE, SIZE, #\decrement @ C flag survives rest of macro
 .endif
        sub     TMP0, DAT0, PATTERN, lsr #14
        sub     TMP1, DAT1, PATTERN, lsr #14
        bic     TMP0, TMP0, DAT0
        bic     TMP1, TMP1, DAT1
        sub     TMP2, DAT2, PATTERN, lsr #14
        sub     TMP3, DAT3, PATTERN, lsr #14
        ands    TMP0, TMP0, PATTERN
        bic     TMP2, TMP2, DAT2
        it      eq
        andseq  TMP1, TMP1, PATTERN
        bic     TMP3, TMP3, DAT3
        itt     eq
        andseq  TMP2, TMP2, PATTERN
        andseq  TMP3, TMP3, PATTERN
.endm

/* int ff_h264_find_start_code_candidate_armv6(const uint8_t *buf, int size) */
function ff_h264_find_start_code_candidate_armv6, export=1
        push    {v1-v6,lr}
        mov     PTR, BUF
        @ Ensure there are at least (PRELOAD_DISTANCE+2) complete cachelines to go
        @ before using code that does preloads
        cmp     SIZE, #(PRELOAD_DISTANCE+3)*32 - 1
        blo     60f

        @ Get to word-alignment, 1 byte at a time
        tst     PTR, #3
        beq     2f
1:      ldrb    DAT0, [PTR], #1
        sub     SIZE, SIZE, #1
        teq     DAT0, #0
        beq     90f
        tst     PTR, #3
        bne     1b
2:      @ Get to 4-word alignment, 1 word at a time
        ldr     PATTERN, =0x80008000
        setend  be
        tst     PTR, #12
        beq     4f
3:      innerloop4
        bne     91f
        tst     PTR, #12
        bne     3b
4:      @ Get to cacheline (8-word) alignment
        tst     PTR, #16
        beq     5f
        innerloop16  16
        bne     93f
5:      @ Check complete cachelines, with preloading
        @ We need to stop when there are still (PRELOAD_DISTANCE+1)
        @ complete cachelines to go
        sub     SIZE, SIZE, #(PRELOAD_DISTANCE+2)*32
6:      innerloop16  , do_preload
        bne     93f
        innerloop16  32
        bne     93f
        bcs     6b
        @ Preload trailing part-cacheline, if any
        tst     SIZE, #31
        beq     7f
        pld     [PTR, #(PRELOAD_DISTANCE+1)*32]
        @ Check remaining data without doing any more preloads. First
        @ do in chunks of 4 words:
7:      adds    SIZE, SIZE, #(PRELOAD_DISTANCE+2)*32 - 16
        bmi     9f
8:      innerloop16  16
        bne     93f
        bcs     8b
        @ Then in words:
9:      adds    SIZE, SIZE, #16 - 4
        bmi     11f
10:     innerloop4
        bne     91f
        bcs     10b
11:     setend  le
        @ Check second byte of final halfword
        ldrb    DAT0, [PTR, #-1]
        teq     DAT0, #0
        beq     90f
        @ Check any remaining bytes
        tst     SIZE, #3
        beq     13f
12:     ldrb    DAT0, [PTR], #1
        sub     SIZE, SIZE, #1
        teq     DAT0, #0
        beq     90f
        tst     SIZE, #3
        bne     12b
        @ No candidate found
13:     sub     RESULT, PTR, BUF
        b       99f

60:     @ Small buffer - simply check by looping over bytes
        subs    SIZE, SIZE, #1
        bcc     99f
61:     ldrb    DAT0, [PTR], #1
        subs    SIZE, SIZE, #1
        teq     DAT0, #0
        beq     90f
        bcs     61b
        @ No candidate found
        sub     RESULT, PTR, BUF
        b       99f

90:     @ Found a candidate at the preceding byte
        sub     RESULT, PTR, BUF
        sub     RESULT, RESULT, #1
        b       99f

91:     @ Found a candidate somewhere in the preceding 4 bytes
        sub     RESULT, PTR, BUF
        sub     RESULT, RESULT, #4
        sub     TMP0, DAT0, #0x20000
        bics    TMP0, TMP0, DAT0
        itt     pl
        ldrbpl  DAT0, [PTR, #-3]
        addpl   RESULT, RESULT, #2
        bpl     92f
        teq     RESULT, #0
        beq     98f @ don't look back a byte if found at first byte in buffer
        ldrb    DAT0, [PTR, #-5]
92:     teq     DAT0, #0
        it      eq
        subeq   RESULT, RESULT, #1
        b       98f

93:     @ Found a candidate somewhere in the preceding 16 bytes
        sub     RESULT, PTR, BUF
        sub     RESULT, RESULT, #16
        teq     TMP0, #0
        beq     95f @ not in first 4 bytes
        sub     TMP0, DAT0, #0x20000
        bics    TMP0, TMP0, DAT0
        itt     pl
        ldrbpl  DAT0, [PTR, #-15]
        addpl   RESULT, RESULT, #2
        bpl     94f
        teq     RESULT, #0
        beq     98f @ don't look back a byte if found at first byte in buffer
        ldrb    DAT0, [PTR, #-17]
94:     teq     DAT0, #0
        it      eq
        subeq   RESULT, RESULT, #1
        b       98f
95:     add     RESULT, RESULT, #4
        teq     TMP1, #0
        beq     96f @ not in next 4 bytes
        sub     TMP1, DAT1, #0x20000
        bics    TMP1, TMP1, DAT1
        itee    mi
        ldrbmi  DAT0, [PTR, #-13]
        ldrbpl  DAT0, [PTR, #-11]
        addpl   RESULT, RESULT, #2
        teq     DAT0, #0
        it      eq
        subeq   RESULT, RESULT, #1
        b       98f
96:     add     RESULT, RESULT, #4
        teq     TMP2, #0
        beq     97f @ not in next 4 bytes
        sub     TMP2, DAT2, #0x20000
        bics    TMP2, TMP2, DAT2
        itee    mi
        ldrbmi  DAT0, [PTR, #-9]
        ldrbpl  DAT0, [PTR, #-7]
        addpl   RESULT, RESULT, #2
        teq     DAT0, #0
        it      eq
        subeq   RESULT, RESULT, #1
        b       98f
97:     add     RESULT, RESULT, #4
        sub     TMP3, DAT3, #0x20000
        bics    TMP3, TMP3, DAT3
        itee    mi
        ldrbmi  DAT0, [PTR, #-5]
        ldrbpl  DAT0, [PTR, #-3]
        addpl   RESULT, RESULT, #2
        teq     DAT0, #0
        it      eq
        subeq   RESULT, RESULT, #1
        @ drop through to 98f
98:     setend  le
99:     pop     {v1-v6,pc}
endfunc

        .unreq  RESULT
        .unreq  BUF
        .unreq  SIZE
        .unreq  PATTERN
        .unreq  PTR
        .unreq  DAT0
        .unreq  DAT1
        .unreq  DAT2
        .unreq  DAT3
        .unreq  TMP0
        .unreq  TMP1
        .unreq  TMP2
        .unreq  TMP3
