/*
   C-like prototype :
        void j_rev_dct_ARM(DCTBLOCK data)

   With DCTBLOCK being a pointer to an array of 64 'signed shorts'

   Copyright (c) 2001 Lionel Ulmer (lionel.ulmer@free.fr / bbrox@bbrox.org)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
   COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
   IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include "asm.S"

#define FIX_0_298631336 2446
#define FIX_0_541196100 4433
#define FIX_0_765366865 6270
#define FIX_1_175875602 9633
#define FIX_1_501321110 12299
#define FIX_2_053119869 16819
#define FIX_3_072711026 25172
#define FIX_M_0_390180644 -3196
#define FIX_M_0_899976223 -7373
#define FIX_M_1_847759065 -15137
#define FIX_M_1_961570560 -16069
#define FIX_M_2_562915447 -20995
#define FIX_0xFFFF 0xFFFF

#define FIX_0_298631336_ID      0
#define FIX_0_541196100_ID      4
#define FIX_0_765366865_ID      8
#define FIX_1_175875602_ID     12
#define FIX_1_501321110_ID     16
#define FIX_2_053119869_ID     20
#define FIX_3_072711026_ID     24
#define FIX_M_0_390180644_ID   28
#define FIX_M_0_899976223_ID   32
#define FIX_M_1_847759065_ID   36
#define FIX_M_1_961570560_ID   40
#define FIX_M_2_562915447_ID   44
#define FIX_0xFFFF_ID          48
        .text
        .align

function j_rev_dct_ARM, export=1
        stmdb   sp!, { r4 - r12, lr }   @ all callee saved regs

        sub sp, sp, #4                  @ reserve some space on the stack
        str r0, [ sp ]                  @ save the DCT pointer to the stack

        mov lr, r0                      @ lr = pointer to the current row
        mov r12, #8                     @ r12 = row-counter
        add r11, pc, #(const_array-.-8) @ r11 = base pointer to the constants array
row_loop:
        ldrsh r0, [lr, # 0]             @ r0 = 'd0'
        ldrsh r2, [lr, # 2]             @ r2 = 'd2'

        @ Optimization for row that have all items except the first set to 0
        @ (this works as the DCTELEMS are always 4-byte aligned)
        ldr r5, [lr, # 0]
        ldr r6, [lr, # 4]
        ldr r3, [lr, # 8]
        ldr r4, [lr, #12]
        orr r3, r3, r4
        orr r3, r3, r6
        orrs r5, r3, r5
        beq end_of_row_loop             @ nothing to be done as ALL of them are '0'
        orrs r3, r3, r2
        beq empty_row

        ldrsh r1, [lr, # 8]             @ r1 = 'd1'
        ldrsh r4, [lr, # 4]             @ r4 = 'd4'
        ldrsh r6, [lr, # 6]             @ r6 = 'd6'

        ldr r3, [r11, #FIX_0_541196100_ID]
        add r7, r2, r6
        ldr r5, [r11, #FIX_M_1_847759065_ID]
        mul r7, r3, r7                      @ r7 = z1
        ldr r3, [r11, #FIX_0_765366865_ID]
        mla r6, r5, r6, r7                  @ r6 = tmp2
        add r5, r0, r4                      @ r5 = tmp0
        mla r2, r3, r2, r7                  @ r2 = tmp3
        sub r3, r0, r4                      @ r3 = tmp1

        add r0, r2, r5, lsl #13             @ r0 = tmp10
        rsb r2, r2, r5, lsl #13             @ r2 = tmp13
        add r4, r6, r3, lsl #13             @ r4 = tmp11
        rsb r3, r6, r3, lsl #13             @ r3 = tmp12

        stmdb   sp!, { r0, r2, r3, r4 } @ save on the stack tmp10, tmp13, tmp12, tmp11

        ldrsh r3, [lr, #10]             @ r3 = 'd3'
        ldrsh r5, [lr, #12]             @ r5 = 'd5'
        ldrsh r7, [lr, #14]             @ r7 = 'd7'

        add r0, r3, r5                        @ r0 = 'z2'
        add r2, r1, r7                  @ r2 = 'z1'
        add r4, r3, r7                  @ r4 = 'z3'
        add r6, r1, r5                  @ r6 = 'z4'
        ldr r9, [r11, #FIX_1_175875602_ID]
        add r8, r4, r6                  @ r8 = z3 + z4
        ldr r10, [r11, #FIX_M_0_899976223_ID]
        mul r8, r9, r8                  @ r8 = 'z5'
        ldr r9, [r11, #FIX_M_2_562915447_ID]
        mul r2, r10, r2                 @ r2 = 'z1'
        ldr r10, [r11, #FIX_M_1_961570560_ID]
        mul r0, r9, r0                  @ r0 = 'z2'
        ldr r9, [r11, #FIX_M_0_390180644_ID]
        mla r4, r10, r4, r8             @ r4 = 'z3'
        ldr r10, [r11, #FIX_0_298631336_ID]
        mla r6, r9, r6, r8              @ r6 = 'z4'
        ldr r9, [r11, #FIX_2_053119869_ID]
        mla r7, r10, r7, r2             @ r7 = tmp0 + z1
        ldr r10, [r11, #FIX_3_072711026_ID]
        mla r5, r9, r5, r0              @ r5 = tmp1 + z2
        ldr r9, [r11, #FIX_1_501321110_ID]
        mla r3, r10, r3, r0             @ r3 = tmp2 + z2
        add r7, r7, r4                  @ r7 = tmp0
        mla r1, r9, r1, r2              @ r1 = tmp3 + z1
        add r5,        r5, r6                  @ r5 = tmp1
        add r3, r3, r4                  @ r3 = tmp2
        add r1, r1, r6                  @ r1 = tmp3

        ldmia sp!, { r0, r2, r4, r6 } @ r0 = tmp10 / r2 = tmp13 / r4 = tmp12 / r6 = tmp11
                                      @ r1 = tmp3  / r3 = tmp2  / r5 = tmp1  / r7 = tmp0

        @ Compute DESCALE(tmp10 + tmp3, CONST_BITS-PASS1_BITS)
        add r8, r0, r1
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, # 0]

        @ Compute DESCALE(tmp10 - tmp3, CONST_BITS-PASS1_BITS)
        sub r8, r0, r1
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, #14]

        @ Compute DESCALE(tmp11 + tmp2, CONST_BITS-PASS1_BITS)
        add r8, r6, r3
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, # 2]

        @ Compute DESCALE(tmp11 - tmp2, CONST_BITS-PASS1_BITS)
        sub r8, r6, r3
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, #12]

        @ Compute DESCALE(tmp12 + tmp1, CONST_BITS-PASS1_BITS)
        add r8, r4, r5
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, # 4]

        @ Compute DESCALE(tmp12 - tmp1, CONST_BITS-PASS1_BITS)
        sub r8, r4, r5
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, #10]

        @ Compute DESCALE(tmp13 + tmp0, CONST_BITS-PASS1_BITS)
        add r8, r2, r7
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, # 6]

        @ Compute DESCALE(tmp13 - tmp0, CONST_BITS-PASS1_BITS)
        sub r8, r2, r7
        add r8, r8, #(1<<10)
        mov r8, r8, asr #11
        strh r8, [lr, # 8]

        @ End of row loop
        add lr, lr, #16
        subs r12, r12, #1
        bne row_loop
        beq start_column_loop

empty_row:
        ldr r1, [r11, #FIX_0xFFFF_ID]
        mov r0, r0, lsl #2
        and r0, r0, r1
        add r0, r0, r0, lsl #16
        str r0, [lr, # 0]
        str r0, [lr, # 4]
        str r0, [lr, # 8]
        str r0, [lr, #12]

end_of_row_loop:
        @ End of loop
        add lr, lr, #16
        subs r12, r12, #1
        bne row_loop

start_column_loop:
        @ Start of column loop
        ldr lr, [ sp ]
        mov r12, #8
column_loop:
        ldrsh r0, [lr, #( 0*8)]             @ r0 = 'd0'
        ldrsh r2, [lr, #( 4*8)]             @ r2 = 'd2'
        ldrsh r4, [lr, #( 8*8)]             @ r4 = 'd4'
        ldrsh r6, [lr, #(12*8)]             @ r6 = 'd6'

        ldr r3, [r11, #FIX_0_541196100_ID]
        add r1, r2, r6
        ldr r5, [r11, #FIX_M_1_847759065_ID]
        mul r1, r3, r1                      @ r1 = z1
        ldr r3, [r11, #FIX_0_765366865_ID]
        mla r6, r5, r6, r1                  @ r6 = tmp2
        add r5, r0, r4                      @ r5 = tmp0
        mla r2, r3, r2, r1                  @ r2 = tmp3
        sub r3, r0, r4                      @ r3 = tmp1

        add r0, r2, r5, lsl #13             @ r0 = tmp10
        rsb r2, r2, r5, lsl #13             @ r2 = tmp13
        add r4, r6, r3, lsl #13             @ r4 = tmp11
        rsb r6, r6, r3, lsl #13             @ r6 = tmp12

        ldrsh r1, [lr, #( 2*8)]             @ r1 = 'd1'
        ldrsh r3, [lr, #( 6*8)]             @ r3 = 'd3'
        ldrsh r5, [lr, #(10*8)]             @ r5 = 'd5'
        ldrsh r7, [lr, #(14*8)]             @ r7 = 'd7'

        @ Check for empty odd column (happens about 20 to 25 % of the time according to my stats)
        orr r9, r1, r3
        orr r10, r5, r7
        orrs r10, r9, r10
        beq empty_odd_column

        stmdb   sp!, { r0, r2, r4, r6 } @ save on the stack tmp10, tmp13, tmp12, tmp11

        add r0, r3, r5                  @ r0 = 'z2'
        add r2, r1, r7                  @ r2 = 'z1'
        add r4, r3, r7                  @ r4 = 'z3'
        add r6, r1, r5                  @ r6 = 'z4'
        ldr r9, [r11, #FIX_1_175875602_ID]
        add r8, r4, r6
        ldr r10, [r11, #FIX_M_0_899976223_ID]
        mul r8, r9, r8                  @ r8 = 'z5'
        ldr r9, [r11, #FIX_M_2_562915447_ID]
        mul r2, r10, r2                 @ r2 = 'z1'
        ldr r10, [r11, #FIX_M_1_961570560_ID]
        mul r0, r9, r0                  @ r0 = 'z2'
        ldr r9, [r11, #FIX_M_0_390180644_ID]
        mla r4, r10, r4, r8             @ r4 = 'z3'
        ldr r10, [r11, #FIX_0_298631336_ID]
        mla r6, r9, r6, r8              @ r6 = 'z4'
        ldr r9, [r11, #FIX_2_053119869_ID]
        mla r7, r10, r7, r2             @ r7 = tmp0 + z1
        ldr r10, [r11, #FIX_3_072711026_ID]
        mla r5, r9, r5, r0              @ r5 = tmp1 + z2
        ldr r9, [r11, #FIX_1_501321110_ID]
        mla r3, r10, r3, r0             @ r3 = tmp2 + z2
        add r7, r7, r4                  @ r7 = tmp0
        mla r1, r9, r1, r2              @ r1 = tmp3 + z1
        add r5,        r5, r6                  @ r5 = tmp1
        add r3, r3, r4                  @ r3 = tmp2
        add r1, r1, r6                  @ r1 = tmp3

        ldmia sp!, { r0, r2, r4, r6 } @ r0 = tmp10 / r2 = tmp13 / r4 = tmp11 / r6 = tmp12
                                      @ r1 = tmp3  / r3 = tmp2  / r5 = tmp1  / r7 = tmp0

        @ Compute DESCALE(tmp10 + tmp3, CONST_BITS+PASS1_BITS+3)
        add r8, r0, r1
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #( 0*8)]

        @ Compute DESCALE(tmp10 - tmp3, CONST_BITS+PASS1_BITS+3)
        sub r8, r0, r1
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #(14*8)]

        @ Compute DESCALE(tmp11 + tmp2, CONST_BITS+PASS1_BITS+3)
        add r8, r4, r3
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #( 2*8)]

        @ Compute DESCALE(tmp11 - tmp2, CONST_BITS+PASS1_BITS+3)
        sub r8, r4, r3
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #(12*8)]

        @ Compute DESCALE(tmp12 + tmp1, CONST_BITS+PASS1_BITS+3)
        add r8, r6, r5
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #( 4*8)]

        @ Compute DESCALE(tmp12 - tmp1, CONST_BITS+PASS1_BITS+3)
        sub r8, r6, r5
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #(10*8)]

        @ Compute DESCALE(tmp13 + tmp0, CONST_BITS+PASS1_BITS+3)
        add r8, r2, r7
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #( 6*8)]

        @ Compute DESCALE(tmp13 - tmp0, CONST_BITS+PASS1_BITS+3)
        sub r8, r2, r7
        add r8, r8, #(1<<17)
        mov r8, r8, asr #18
        strh r8, [lr, #( 8*8)]

        @ End of row loop
        add lr, lr, #2
        subs r12, r12, #1
        bne column_loop
        beq the_end

empty_odd_column:
        @ Compute DESCALE(tmp10 + tmp3, CONST_BITS+PASS1_BITS+3)
        @ Compute DESCALE(tmp10 - tmp3, CONST_BITS+PASS1_BITS+3)
        add r0, r0, #(1<<17)
        mov r0, r0, asr #18
        strh r0, [lr, #( 0*8)]
        strh r0, [lr, #(14*8)]

        @ Compute DESCALE(tmp11 + tmp2, CONST_BITS+PASS1_BITS+3)
        @ Compute DESCALE(tmp11 - tmp2, CONST_BITS+PASS1_BITS+3)
        add r4, r4, #(1<<17)
        mov r4, r4, asr #18
        strh r4, [lr, #( 2*8)]
        strh r4, [lr, #(12*8)]

        @ Compute DESCALE(tmp12 + tmp1, CONST_BITS+PASS1_BITS+3)
        @ Compute DESCALE(tmp12 - tmp1, CONST_BITS+PASS1_BITS+3)
        add r6, r6, #(1<<17)
        mov r6, r6, asr #18
        strh r6, [lr, #( 4*8)]
        strh r6, [lr, #(10*8)]

        @ Compute DESCALE(tmp13 + tmp0, CONST_BITS+PASS1_BITS+3)
        @ Compute DESCALE(tmp13 - tmp0, CONST_BITS+PASS1_BITS+3)
        add r2, r2, #(1<<17)
        mov r2, r2, asr #18
        strh r2, [lr, #( 6*8)]
        strh r2, [lr, #( 8*8)]

        @ End of row loop
        add lr, lr, #2
        subs r12, r12, #1
        bne column_loop

the_end:
        @ The end....
        add sp, sp, #4
        ldmia   sp!, { r4 - r12, pc }   @ restore callee saved regs and return

const_array:
        .align
        .word FIX_0_298631336
        .word FIX_0_541196100
        .word FIX_0_765366865
        .word FIX_1_175875602
        .word FIX_1_501321110
        .word FIX_2_053119869
        .word FIX_3_072711026
        .word FIX_M_0_390180644
        .word FIX_M_0_899976223
        .word FIX_M_1_847759065
        .word FIX_M_1_961570560
        .word FIX_M_2_562915447
        .word FIX_0xFFFF
