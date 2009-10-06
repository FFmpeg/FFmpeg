/*
 * simple_idct_arm.S
 * Copyright (C) 2002 Frederic 'dilb' Boulay
 *
 * Author: Frederic Boulay <dilb@handhelds.org>
 *
 * The function defined in this file is derived from the simple_idct function
 * from the libavcodec library part of the FFmpeg project.
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

#include "asm.S"

/* useful constants for the algorithm, they are save in __constant_ptr__ at */
/* the end of the source code.*/
#define W1  22725
#define W2  21407
#define W3  19266
#define W4  16383
#define W5  12873
#define W6  8867
#define W7  4520
#define MASK_MSHW 0xFFFF0000

/* offsets of the constants in the vector */
#define offW1  0
#define offW2  4
#define offW3  8
#define offW4  12
#define offW5  16
#define offW6  20
#define offW7  24
#define offMASK_MSHW 28

#define ROW_SHIFT 11
#define ROW_SHIFT2MSHW (16-11)
#define COL_SHIFT 20
#define ROW_SHIFTED_1 1024 /* 1<< (ROW_SHIFT-1) */
#define COL_SHIFTED_1 524288 /* 1<< (COL_SHIFT-1) */


        .text

function ff_simple_idct_arm, export=1
        @@ void simple_idct_arm(int16_t *block)
        @@ save stack for reg needed (take all of them),
        @@ R0-R3 are scratch regs, so no need to save them, but R0 contains the pointer to block
        @@ so it must not be overwritten, if it is not saved!!
        @@ R12 is another scratch register, so it should not be saved too
        @@ save all registers
        stmfd sp!, {r4-r11, r14} @ R14 is also called LR
        @@ at this point, R0=block, other registers are free.
        add r14, r0, #112        @ R14=&block[8*7], better start from the last row, and decrease the value until row=0, i.e. R12=block.
        adr r12, __constant_ptr__ @ R12=__constant_ptr__, the vector containing the constants, probably not necessary to reserve a register for it
        @@ add 2 temporary variables in the stack: R0 and R14
        sub sp, sp, #8          @ allow 2 local variables
        str r0, [sp, #0]        @ save block in sp[0]
        @@ stack status
        @@ sp+4   free
        @@ sp+0   R0  (block)


        @@ at this point, R0=block, R14=&block[56], R12=__const_ptr_, R1-R11 free


__row_loop:
        @@ read the row and check if it is null, almost null, or not, according to strongarm specs, it is not necessary to optimize ldr accesses (i.e. split 32bits in 2 16bits words), at least it gives more usable registers :)
        ldr r1, [r14, #0]        @ R1=(int32)(R12)[0]=ROWr32[0] (relative row cast to a 32b pointer)
        ldr r2, [r14, #4]        @ R2=(int32)(R12)[1]=ROWr32[1]
        ldr r3, [r14, #8]        @ R3=ROWr32[2]
        ldr r4, [r14, #12]       @ R4=ROWr32[3]
        @@ check if the words are null, if all of them are null, then proceed with next row (branch __end_row_loop),
        @@ if ROWr16[0] is the only one not null, then proceed with this special case (branch __almost_empty_row)
        @@ else follow the complete algorithm.
        @@ at this point, R0=block, R14=&block[n], R12=__const_ptr_, R1=ROWr32[0], R2=ROWr32[1],
        @@                R3=ROWr32[2], R4=ROWr32[3], R5-R11 free
        orr r5, r4, r3           @ R5=R4 | R3
        orr r5, r5, r2           @ R5=R4 | R3 | R2
        orrs r6, r5, r1          @ Test R5 | R1 (the aim is to check if everything is null)
        beq __end_row_loop
        mov r7, r1, asr #16      @ R7=R1>>16=ROWr16[1] (evaluate it now, as it could be useful later)
        ldrsh r6, [r14, #0]      @ R6=ROWr16[0]
        orrs r5, r5, r7          @ R5=R4 | R3 | R2 | R7
        beq __almost_empty_row

__b_evaluation:
        @@ at this point, R0=block (temp),  R1(free), R2=ROWr32[1], R3=ROWr32[2], R4=ROWr32[3],
        @@     R5=(temp), R6=ROWr16[0], R7=ROWr16[1], R8-R11 free,
        @@     R12=__const_ptr_, R14=&block[n]
        @@ to save some registers/calls, proceed with b0-b3 first, followed by a0-a3

        @@ MUL16(b0, W1, row[1]);
        @@ MUL16(b1, W3, row[1]);
        @@ MUL16(b2, W5, row[1]);
        @@ MUL16(b3, W7, row[1]);
        @@ MAC16(b0, W3, row[3]);
        @@ MAC16(b1, -W7, row[3]);
        @@ MAC16(b2, -W1, row[3]);
        @@ MAC16(b3, -W5, row[3]);
        ldr r8, [r12, #offW1]    @ R8=W1
        mov r2, r2, asr #16      @ R2=ROWr16[3]
        mul r0, r8, r7           @ R0=W1*ROWr16[1]=b0 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        ldr r9, [r12, #offW3]    @ R9=W3
        ldr r10, [r12, #offW5]   @ R10=W5
        mul r1, r9, r7           @ R1=W3*ROWr16[1]=b1 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        ldr r11, [r12, #offW7]   @ R11=W7
        mul r5, r10, r7          @ R5=W5*ROWr16[1]=b2 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        mul r7, r11, r7          @ R7=W7*ROWr16[1]=b3 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
                teq r2, #0               @ if null avoid muls
                mlane r0, r9, r2, r0     @ R0+=W3*ROWr16[3]=b0 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        rsbne r2, r2, #0         @ R2=-ROWr16[3]
        mlane r1, r11, r2, r1    @ R1-=W7*ROWr16[3]=b1 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        mlane r5, r8, r2, r5     @ R5-=W1*ROWr16[3]=b2 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        mlane r7, r10, r2, r7    @ R7-=W5*ROWr16[3]=b3 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)

        @@ at this point, R0=b0,  R1=b1, R2 (free), R3=ROWr32[2], R4=ROWr32[3],
        @@     R5=b2, R6=ROWr16[0], R7=b3, R8=W1, R9=W3, R10=W5, R11=W7,
        @@     R12=__const_ptr_, R14=&block[n]
        @@ temp = ((uint32_t*)row)[2] | ((uint32_t*)row)[3];
        @@ if (temp != 0) {}
        orrs r2, r3, r4          @ R2=ROWr32[2] | ROWr32[3]
        beq __end_b_evaluation

        @@ at this point, R0=b0,  R1=b1, R2 (free), R3=ROWr32[2], R4=ROWr32[3],
        @@     R5=b2, R6=ROWr16[0], R7=b3, R8=W1, R9=W3, R10=W5, R11=W7,
        @@     R12=__const_ptr_, R14=&block[n]
        @@ MAC16(b0, W5, row[5]);
        @@ MAC16(b2, W7, row[5]);
        @@ MAC16(b3, W3, row[5]);
        @@ MAC16(b1, -W1, row[5]);
        @@ MAC16(b0, W7, row[7]);
        @@ MAC16(b2, W3, row[7]);
        @@ MAC16(b3, -W1, row[7]);
        @@ MAC16(b1, -W5, row[7]);
        mov r3, r3, asr #16      @ R3=ROWr16[5]
                teq r3, #0               @ if null avoid muls
        mlane r0, r10, r3, r0    @ R0+=W5*ROWr16[5]=b0
        mov r4, r4, asr #16      @ R4=ROWr16[7]
        mlane r5, r11, r3, r5    @ R5+=W7*ROWr16[5]=b2
        mlane r7, r9, r3, r7     @ R7+=W3*ROWr16[5]=b3
        rsbne r3, r3, #0         @ R3=-ROWr16[5]
        mlane r1, r8, r3, r1     @ R7-=W1*ROWr16[5]=b1
        @@ R3 is free now
                teq r4, #0               @ if null avoid muls
        mlane r0, r11, r4, r0    @ R0+=W7*ROWr16[7]=b0
        mlane r5, r9, r4, r5     @ R5+=W3*ROWr16[7]=b2
        rsbne r4, r4, #0         @ R4=-ROWr16[7]
        mlane r7, r8, r4, r7     @ R7-=W1*ROWr16[7]=b3
        mlane r1, r10, r4, r1    @ R1-=W5*ROWr16[7]=b1
        @@ R4 is free now
__end_b_evaluation:
        @@ at this point, R0=b0,  R1=b1, R2=ROWr32[2] | ROWr32[3] (tmp), R3 (free), R4 (free),
        @@     R5=b2, R6=ROWr16[0], R7=b3, R8 (free), R9 (free), R10 (free), R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]

__a_evaluation:
        @@ a0 = (W4 * row[0]) + (1 << (ROW_SHIFT - 1));
        @@ a1 = a0 + W6 * row[2];
        @@ a2 = a0 - W6 * row[2];
        @@ a3 = a0 - W2 * row[2];
        @@ a0 = a0 + W2 * row[2];
        ldr r9, [r12, #offW4]    @ R9=W4
        mul r6, r9, r6           @ R6=W4*ROWr16[0]
        ldr r10, [r12, #offW6]   @ R10=W6
        ldrsh r4, [r14, #4]      @ R4=ROWr16[2] (a3 not defined yet)
        add r6, r6, #ROW_SHIFTED_1 @ R6=W4*ROWr16[0] + 1<<(ROW_SHIFT-1) (a0)

        mul r11, r10, r4         @ R11=W6*ROWr16[2]
        ldr r8, [r12, #offW2]    @ R8=W2
        sub r3, r6, r11          @ R3=a0-W6*ROWr16[2] (a2)
        @@ temp = ((uint32_t*)row)[2] | ((uint32_t*)row)[3];
        @@ if (temp != 0) {}
        teq r2, #0
        beq __end_bef_a_evaluation

        add r2, r6, r11          @ R2=a0+W6*ROWr16[2] (a1)
        mul r11, r8, r4          @ R11=W2*ROWr16[2]
        sub r4, r6, r11          @ R4=a0-W2*ROWr16[2] (a3)
        add r6, r6, r11          @ R6=a0+W2*ROWr16[2] (a0)


        @@ at this point, R0=b0,  R1=b1, R2=a1, R3=a2, R4=a3,
        @@     R5=b2, R6=a0, R7=b3, R8=W2, R9=W4, R10=W6, R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]


        @@ a0 += W4*row[4]
        @@ a1 -= W4*row[4]
        @@ a2 -= W4*row[4]
        @@ a3 += W4*row[4]
        ldrsh r11, [r14, #8]     @ R11=ROWr16[4]
                teq r11, #0              @ if null avoid muls
        mulne r11, r9, r11       @ R11=W4*ROWr16[4]
        @@ R9 is free now
        ldrsh r9, [r14, #12]     @ R9=ROWr16[6]
        addne r6, r6, r11        @ R6+=W4*ROWr16[4] (a0)
        subne r2, r2, r11        @ R2-=W4*ROWr16[4] (a1)
        subne r3, r3, r11        @ R3-=W4*ROWr16[4] (a2)
        addne r4, r4, r11        @ R4+=W4*ROWr16[4] (a3)
        @@ W6 alone is no more useful, save W2*ROWr16[6] in it instead
                teq r9, #0               @ if null avoid muls
        mulne r11, r10, r9       @ R11=W6*ROWr16[6]
        addne r6, r6, r11        @ R6+=W6*ROWr16[6] (a0)
        mulne r10, r8, r9        @ R10=W2*ROWr16[6]
        @@ a0 += W6*row[6];
        @@ a3 -= W6*row[6];
        @@ a1 -= W2*row[6];
        @@ a2 += W2*row[6];
        subne r4, r4, r11        @ R4-=W6*ROWr16[6] (a3)
        subne r2, r2, r10        @ R2-=W2*ROWr16[6] (a1)
        addne r3, r3, r10        @ R3+=W2*ROWr16[6] (a2)

__end_a_evaluation:
        @@ at this point, R0=b0,  R1=b1, R2=a1, R3=a2, R4=a3,
        @@     R5=b2, R6=a0, R7=b3, R8 (free), R9 (free), R10 (free), R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]
        @@ row[0] = (a0 + b0) >> ROW_SHIFT;
        @@ row[1] = (a1 + b1) >> ROW_SHIFT;
        @@ row[2] = (a2 + b2) >> ROW_SHIFT;
        @@ row[3] = (a3 + b3) >> ROW_SHIFT;
        @@ row[4] = (a3 - b3) >> ROW_SHIFT;
        @@ row[5] = (a2 - b2) >> ROW_SHIFT;
        @@ row[6] = (a1 - b1) >> ROW_SHIFT;
        @@ row[7] = (a0 - b0) >> ROW_SHIFT;
        add r8, r6, r0           @ R8=a0+b0
        add r9, r2, r1           @ R9=a1+b1
        @@ put 2 16 bits half-words in a 32bits word
        @@ ROWr32[0]=ROWr16[0] | (ROWr16[1]<<16) (only Little Endian compliant then!!!)
        ldr r10, [r12, #offMASK_MSHW] @ R10=0xFFFF0000
        and r9, r10, r9, lsl #ROW_SHIFT2MSHW @ R9=0xFFFF0000 & ((a1+b1)<<5)
        mvn r11, r10             @ R11= NOT R10= 0x0000FFFF
        and r8, r11, r8, asr #ROW_SHIFT @ R8=0x0000FFFF & ((a0+b0)>>11)
        orr r8, r8, r9
        str r8, [r14, #0]

        add r8, r3, r5           @ R8=a2+b2
        add r9, r4, r7           @ R9=a3+b3
        and r9, r10, r9, lsl #ROW_SHIFT2MSHW @ R9=0xFFFF0000 & ((a3+b3)<<5)
        and r8, r11, r8, asr #ROW_SHIFT @ R8=0x0000FFFF & ((a2+b2)>>11)
        orr r8, r8, r9
        str r8, [r14, #4]

        sub r8, r4, r7           @ R8=a3-b3
        sub r9, r3, r5           @ R9=a2-b2
        and r9, r10, r9, lsl #ROW_SHIFT2MSHW @ R9=0xFFFF0000 & ((a2-b2)<<5)
        and r8, r11, r8, asr #ROW_SHIFT @ R8=0x0000FFFF & ((a3-b3)>>11)
        orr r8, r8, r9
        str r8, [r14, #8]

        sub r8, r2, r1           @ R8=a1-b1
        sub r9, r6, r0           @ R9=a0-b0
        and r9, r10, r9, lsl #ROW_SHIFT2MSHW @ R9=0xFFFF0000 & ((a0-b0)<<5)
        and r8, r11, r8, asr #ROW_SHIFT @ R8=0x0000FFFF & ((a1-b1)>>11)
        orr r8, r8, r9
        str r8, [r14, #12]

        bal __end_row_loop

__almost_empty_row:
        @@ the row was empty, except ROWr16[0], now, management of this special case
        @@ at this point, R0=block, R14=&block[n], R12=__const_ptr_, R1=ROWr32[0], R2=ROWr32[1],
        @@                R3=ROWr32[2], R4=ROWr32[3], R5=(temp), R6=ROWr16[0], R7=ROWr16[1],
        @@                R8=0xFFFF (temp), R9-R11 free
        mov r8, #0x10000         @ R8=0xFFFF (2 steps needed!) it saves a ldr call (because of delay run).
        sub r8, r8, #1           @ R8 is now ready.
        and r5, r8, r6, lsl #3   @ R5=R8 & (R6<<3)= (ROWr16[0]<<3) & 0xFFFF
        orr r5, r5, r5, lsl #16  @ R5=R5 | (R5<<16)
        str r5, [r14, #0]        @ R14[0]=ROWr32[0]=R5
        str r5, [r14, #4]        @ R14[4]=ROWr32[1]=R5
        str r5, [r14, #8]        @ R14[8]=ROWr32[2]=R5
        str r5, [r14, #12]       @ R14[12]=ROWr32[3]=R5

__end_row_loop:
        @@ at this point, R0-R11 (free)
        @@     R12=__const_ptr_, R14=&block[n]
        ldr r0, [sp, #0]         @ R0=block
        teq r0, r14              @ compare current &block[8*n] to block, when block is reached, the loop is finished.
        sub r14, r14, #16
        bne __row_loop



        @@ at this point, R0=block, R1-R11 (free)
        @@     R12=__const_ptr_, R14=&block[n]
        add r14, r0, #14        @ R14=&block[7], better start from the last col, and decrease the value until col=0, i.e. R14=block.
__col_loop:

__b_evaluation2:
        @@ at this point, R0=block (temp),  R1-R11 (free)
        @@     R12=__const_ptr_, R14=&block[n]
        @@ proceed with b0-b3 first, followed by a0-a3
        @@ MUL16(b0, W1, col[8x1]);
        @@ MUL16(b1, W3, col[8x1]);
        @@ MUL16(b2, W5, col[8x1]);
        @@ MUL16(b3, W7, col[8x1]);
        @@ MAC16(b0, W3, col[8x3]);
        @@ MAC16(b1, -W7, col[8x3]);
        @@ MAC16(b2, -W1, col[8x3]);
        @@ MAC16(b3, -W5, col[8x3]);
        ldr r8, [r12, #offW1]    @ R8=W1
        ldrsh r7, [r14, #16]
        mul r0, r8, r7           @ R0=W1*ROWr16[1]=b0 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        ldr r9, [r12, #offW3]    @ R9=W3
        ldr r10, [r12, #offW5]   @ R10=W5
        mul r1, r9, r7           @ R1=W3*ROWr16[1]=b1 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        ldr r11, [r12, #offW7]   @ R11=W7
        mul r5, r10, r7          @ R5=W5*ROWr16[1]=b2 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        ldrsh r2, [r14, #48]
        mul r7, r11, r7          @ R7=W7*ROWr16[1]=b3 (ROWr16[1] must be the second arg, to have the possibility to save 1 cycle)
        teq r2, #0               @ if 0, then avoid muls
        mlane r0, r9, r2, r0     @ R0+=W3*ROWr16[3]=b0 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        rsbne r2, r2, #0         @ R2=-ROWr16[3]
        mlane r1, r11, r2, r1    @ R1-=W7*ROWr16[3]=b1 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        mlane r5, r8, r2, r5     @ R5-=W1*ROWr16[3]=b2 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)
        mlane r7, r10, r2, r7    @ R7-=W5*ROWr16[3]=b3 (ROWr16[3] must be the second arg, to have the possibility to save 1 cycle)

        @@ at this point, R0=b0,  R1=b1, R2 (free), R3 (free), R4 (free),
        @@     R5=b2, R6 (free), R7=b3, R8=W1, R9=W3, R10=W5, R11=W7,
        @@     R12=__const_ptr_, R14=&block[n]
        @@ MAC16(b0, W5, col[5x8]);
        @@ MAC16(b2, W7, col[5x8]);
        @@ MAC16(b3, W3, col[5x8]);
        @@ MAC16(b1, -W1, col[5x8]);
        @@ MAC16(b0, W7, col[7x8]);
        @@ MAC16(b2, W3, col[7x8]);
        @@ MAC16(b3, -W1, col[7x8]);
        @@ MAC16(b1, -W5, col[7x8]);
        ldrsh r3, [r14, #80]     @ R3=COLr16[5x8]
        teq r3, #0               @ if 0 then avoid muls
        mlane r0, r10, r3, r0    @ R0+=W5*ROWr16[5x8]=b0
        mlane r5, r11, r3, r5    @ R5+=W7*ROWr16[5x8]=b2
        mlane r7, r9, r3, r7     @ R7+=W3*ROWr16[5x8]=b3
        rsbne r3, r3, #0         @ R3=-ROWr16[5x8]
        ldrsh r4, [r14, #112]    @ R4=COLr16[7x8]
        mlane r1, r8, r3, r1     @ R7-=W1*ROWr16[5x8]=b1
        @@ R3 is free now
        teq r4, #0               @ if 0 then avoid muls
        mlane r0, r11, r4, r0    @ R0+=W7*ROWr16[7x8]=b0
        mlane r5, r9, r4, r5     @ R5+=W3*ROWr16[7x8]=b2
        rsbne r4, r4, #0         @ R4=-ROWr16[7x8]
        mlane r7, r8, r4, r7     @ R7-=W1*ROWr16[7x8]=b3
        mlane r1, r10, r4, r1    @ R1-=W5*ROWr16[7x8]=b1
        @@ R4 is free now
__end_b_evaluation2:
        @@ at this point, R0=b0,  R1=b1, R2 (free), R3 (free), R4 (free),
        @@     R5=b2, R6 (free), R7=b3, R8 (free), R9 (free), R10 (free), R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]

__a_evaluation2:
        @@ a0 = (W4 * col[8x0]) + (1 << (COL_SHIFT - 1));
        @@ a1 = a0 + W6 * row[2];
        @@ a2 = a0 - W6 * row[2];
        @@ a3 = a0 - W2 * row[2];
        @@ a0 = a0 + W2 * row[2];
        ldrsh r6, [r14, #0]
        ldr r9, [r12, #offW4]    @ R9=W4
        mul r6, r9, r6           @ R6=W4*ROWr16[0]
        ldr r10, [r12, #offW6]   @ R10=W6
        ldrsh r4, [r14, #32]     @ R4=ROWr16[2] (a3 not defined yet)
        add r6, r6, #COL_SHIFTED_1 @ R6=W4*ROWr16[0] + 1<<(COL_SHIFT-1) (a0)
        mul r11, r10, r4         @ R11=W6*ROWr16[2]
        ldr r8, [r12, #offW2]    @ R8=W2
        add r2, r6, r11          @ R2=a0+W6*ROWr16[2] (a1)
        sub r3, r6, r11          @ R3=a0-W6*ROWr16[2] (a2)
        mul r11, r8, r4          @ R11=W2*ROWr16[2]
        sub r4, r6, r11          @ R4=a0-W2*ROWr16[2] (a3)
        add r6, r6, r11          @ R6=a0+W2*ROWr16[2] (a0)

        @@ at this point, R0=b0,  R1=b1, R2=a1, R3=a2, R4=a3,
        @@     R5=b2, R6=a0, R7=b3, R8=W2, R9=W4, R10=W6, R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]
        @@ a0 += W4*row[4]
        @@ a1 -= W4*row[4]
        @@ a2 -= W4*row[4]
        @@ a3 += W4*row[4]
        ldrsh r11, [r14, #64]    @ R11=ROWr16[4]
        teq r11, #0              @ if null avoid muls
        mulne r11, r9, r11       @ R11=W4*ROWr16[4]
        @@ R9 is free now
        addne r6, r6, r11        @ R6+=W4*ROWr16[4] (a0)
        subne r2, r2, r11        @ R2-=W4*ROWr16[4] (a1)
        subne r3, r3, r11        @ R3-=W4*ROWr16[4] (a2)
        ldrsh r9, [r14, #96]     @ R9=ROWr16[6]
        addne r4, r4, r11        @ R4+=W4*ROWr16[4] (a3)
        @@ W6 alone is no more useful, save W2*ROWr16[6] in it instead
        teq r9, #0               @ if null avoid muls
        mulne r11, r10, r9       @ R11=W6*ROWr16[6]
        addne r6, r6, r11        @ R6+=W6*ROWr16[6] (a0)
        mulne r10, r8, r9        @ R10=W2*ROWr16[6]
        @@ a0 += W6*row[6];
        @@ a3 -= W6*row[6];
        @@ a1 -= W2*row[6];
        @@ a2 += W2*row[6];
        subne r4, r4, r11        @ R4-=W6*ROWr16[6] (a3)
        subne r2, r2, r10        @ R2-=W2*ROWr16[6] (a1)
        addne r3, r3, r10        @ R3+=W2*ROWr16[6] (a2)
__end_a_evaluation2:
        @@ at this point, R0=b0,  R1=b1, R2=a1, R3=a2, R4=a3,
        @@     R5=b2, R6=a0, R7=b3, R8 (free), R9 (free), R10 (free), R11 (free),
        @@     R12=__const_ptr_, R14=&block[n]
        @@ col[0 ] = ((a0 + b0) >> COL_SHIFT);
        @@ col[8 ] = ((a1 + b1) >> COL_SHIFT);
        @@ col[16] = ((a2 + b2) >> COL_SHIFT);
        @@ col[24] = ((a3 + b3) >> COL_SHIFT);
        @@ col[32] = ((a3 - b3) >> COL_SHIFT);
        @@ col[40] = ((a2 - b2) >> COL_SHIFT);
        @@ col[48] = ((a1 - b1) >> COL_SHIFT);
        @@ col[56] = ((a0 - b0) >> COL_SHIFT);
        @@@@@ no optimization here @@@@@
        add r8, r6, r0           @ R8=a0+b0
        add r9, r2, r1           @ R9=a1+b1
        mov r8, r8, asr #COL_SHIFT
        mov r9, r9, asr #COL_SHIFT
        strh r8, [r14, #0]
        strh r9, [r14, #16]
        add r8, r3, r5           @ R8=a2+b2
        add r9, r4, r7           @ R9=a3+b3
        mov r8, r8, asr #COL_SHIFT
        mov r9, r9, asr #COL_SHIFT
        strh r8, [r14, #32]
        strh r9, [r14, #48]
        sub r8, r4, r7           @ R8=a3-b3
        sub r9, r3, r5           @ R9=a2-b2
        mov r8, r8, asr #COL_SHIFT
        mov r9, r9, asr #COL_SHIFT
        strh r8, [r14, #64]
        strh r9, [r14, #80]
        sub r8, r2, r1           @ R8=a1-b1
        sub r9, r6, r0           @ R9=a0-b0
        mov r8, r8, asr #COL_SHIFT
        mov r9, r9, asr #COL_SHIFT
        strh r8, [r14, #96]
        strh r9, [r14, #112]

__end_col_loop:
        @@ at this point, R0-R11 (free)
        @@     R12=__const_ptr_, R14=&block[n]
        ldr r0, [sp, #0]         @ R0=block
        teq r0, r14              @ compare current &block[n] to block, when block is reached, the loop is finished.
        sub r14, r14, #2
        bne __col_loop




__end_simple_idct_arm:
        @@ restore registers to previous status!
        add sp, sp, #8 @@ the local variables!
        ldmfd sp!, {r4-r11, r15} @@ update PC with LR content.



@@ kind of sub-function, here not to overload the common case.
__end_bef_a_evaluation:
        add r2, r6, r11          @ R2=a0+W6*ROWr16[2] (a1)
        mul r11, r8, r4          @ R11=W2*ROWr16[2]
        sub r4, r6, r11          @ R4=a0-W2*ROWr16[2] (a3)
        add r6, r6, r11          @ R6=a0+W2*ROWr16[2] (a0)
        bal __end_a_evaluation


__constant_ptr__:  @@ see #defines at the beginning of the source code for values.
        .align
        .word   W1
        .word   W2
        .word   W3
        .word   W4
        .word   W5
        .word   W6
        .word   W7
        .word   MASK_MSHW
