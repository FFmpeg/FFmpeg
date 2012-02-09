/*
 * vp3_idct BlackFin
 *
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
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
/*
   This blackfin DSP code implements an 8x8 inverse type II DCT.

Prototype       : void ff_bfin_vp3_idct(DCTELEM *in)

Registers Used  : A0, A1, R0-R7, I0-I3, B0, B2, B3, M0-M2, L0-L3, P0-P5, LC0.

*/

#include "config.h"
#include "config_bfin.h"

#if defined(__FDPIC__) && CONFIG_SRAM
.section .l1.data.B,"aw",@progbits
#else
.data
#endif

.align 4;
coefs:
.short 0x5a82;           //  C4
.short 0x5a82;           //  C4
.short 0x30FC;           //cos(3pi/8)  C6
.short 0x7642;           //cos(pi/8)   C2
.short 0x18F9;           //cos(7pi/16)
.short 0x7D8A;           //cos(pi/16)
.short 0x471D;           //cos(5pi/16)
.short 0x6A6E;           //cos(3pi/16)
.short 0x18F9;           //cos(7pi/16)
.short 0x7D8A;           //cos(pi/16)

#if defined(__FDPIC__) && CONFIG_SRAM
.section .l1.data.A
#endif

vtmp: .space 256

#define TMP0 FP-8
#define TMP1 FP-12
#define TMP2 FP-16


.text
DEFUN(vp3_idct,mL1,
        (DCTELEM *block)):

/********************** Function Prologue *********************************/
    link 16;
    [--SP] = (R7:4, P5:3);   // Push the registers onto the stack.
    B0 = R0;                 // Pointer to Input matrix
    RELOC(R1, P3, coefs);    // Pointer to Coefficients
    RELOC(R2, P3, vtmp);     // Pointer to Temporary matrix
    B3 = R1;
    B2 = R2;
    L3 = 20;                // L3 is used for making the coefficient array
                            // circular.
                            // MUST BE RESTORED TO ZERO at function exit.
    M1 = 16 (X);            // All these registers are initialized for
    M3 = 8(X);              // modifying address offsets.

    I0 = B0;                // I0 points to Input Element (0, 0).
    I2 = B0;                // I2 points to Input Element (0, 0).
    I2 += M3 || R0.H = W[I0];
                            // Element 0 is read into R0.H
    I1 = I2;                // I1 points to input Element (0, 6).
    I1 += 4  || R0.L = W[I2++];
                            // I2 points to input Element (0, 4).
                            // Element 4 is read into R0.L.
    P2 = 8 (X);
    P3 = 32 (X);
    P4 = -32 (X);
    P5 = 98 (X);
    R7 = 0x8000(Z);
    I3 = B3;                // I3 points to Coefficients
    P0 = B2;                // P0 points to array Element (0, 0) of temp
    P1 = B2;
    R7 = [I3++] || [TMP2]=R7;            // Coefficient C4 is read into R7.H and R7.L.
    MNOP;
    NOP;

    /*
     *   A1 =      Y0 * cos(pi/4)
     *   A0 =      Y0 * cos(pi/4)
     *   A1 = A1 + Y4 * cos(pi/4)
     *   A0 = A0 - Y4 * cos(pi/4)
     *   load:
     *     R1=(Y2,Y6)
     *     R7=(C2,C6)
     *   res:
     *     R3=Y0, R2=Y4
     */
    A1=R7.H*R0.H,       A0=R7.H*R0.H (IS)       || I0+= 4       || R1.L=W[I1++];
    R3=(A1+=R7.H*R0.L), R2=(A0-=R7.H*R0.L) (IS) || R1.H=W[I0--] || R7=[I3++];

    LSETUP (.0, .1) LC0 = P2; // perform 8 1d idcts

    P2 = 112 (X);
    P1 = P1 + P2;           // P1 points to element (7, 0) of temp buffer.
    P2 = -94(X);

.0:
       /*
        *   A1 =      Y2 * cos(3pi/8)
        *   A0 =      Y2 * cos(pi/8)
        *   A1 = A1 - Y6 * cos(pi/8)
        *   A0 = A0 + Y6 * cos(3pi/8)
        *      R5 = (Y1,Y7)
        *      R7 = (C1,C7)
        *   res:
        *      R1=Y2, R0=Y6
        */
        A1=R7.L*R1.H,       A0=R7.H*R1.H (IS)        || I0+=4        || R5.H=W[I0];
        R1=(A1-=R7.H*R1.L), R0=(A0+=R7.L*R1.L) (IS)  || R5.L=W[I1--] || R7=[I3++];
        /*
        *   Y0 = Y0 + Y6.
        *   Y4 = Y4 + Y2.
        *   Y2 = Y4 - Y2.
        *   Y6 = Y0 - Y6.
        *     R3 is saved
        *     R6.l=Y3
        * note: R3: Y0, R2: Y4, R1: Y2, R0: Y6
        */
        R3=R3+R0, R0=R3-R0;
        R2=R2+R1, R1=R2-R1 || [TMP0]=R3 || R6.L=W[I0--];
        /*
         *  Compute the odd portion (1,3,5,7) even is done.
         *
         *  Y1 = C7 * Y1 - C1 * Y7 + C3 * Y5 - C5 * Y3.
         *  Y7 = C1 * Y1 + C7 * Y7 + C5 * Y5 + C3 * Y3.
         *  Y5 = C5 * Y1 + C3 * Y7 + C7 * Y5 - C1 * Y3.
         *  Y3 = C3 * Y1 - C5 * Y7 - C1 * Y5 - C7 * Y3.
         */
        //  R5=(Y1,Y7)  R6=(Y5,Y3)                                                   // R7=(C1,C7)
        A1 =R7.L*R5.H,       A0 =R7.H*R5.H (IS)       || [TMP1]=R2 || R6.H=W[I2--];
        A1-=R7.H*R5.L,       A0+=R7.L*R5.L (IS)       || I0-=4     || R7=[I3++];
        A1+=R7.H*R6.H,       A0+=R7.L*R6.H (IS)       || I0+=M1;                     // R7=(C3,C5)
        R3 =(A1-=R7.L*R6.L), R2 =(A0+=R7.H*R6.L) (IS);
        A1 =R7.L*R5.H,       A0 =R7.H*R5.H (IS)       || R4=[TMP0];
        A1+=R7.H*R5.L,       A0-=R7.L*R5.L (IS)       || I1+=M1    || R7=[I3++];     // R7=(C1,C7)
        A1+=R7.L*R6.H,       A0-=R7.H*R6.H (IS);
        R7 =(A1-=R7.H*R6.L), R6 =(A0-=R7.L*R6.L) (IS) || I2+=M1;
        // R3=Y1, R2=Y7, R7=Y5, R6=Y3

        /* Transpose write column. */
        R5.H=R4+R2 (RND12);                                   // Y0=Y0+Y7
        R5.L=R4-R2 (RND12) || R4 = [TMP1];                    // Y7=Y7-Y0
        R2.H=R1+R7 (RND12) || W[P0++P3]=R5.H;                 // Y2=Y2+Y5 st Y0
        R2.L=R1-R7 (RND12) || W[P1++P4]=R5.L || R7=[I3++];    // Y5=Y2-Y5 st Y7
        R5.H=R0-R3 (RND12) || W[P0++P3]=R2.H || R1.L=W[I1++]; // Y1=Y6-Y1 st Y2
        R5.L=R0+R3 (RND12) || W[P1++P4]=R2.L || R0.H=W[I0++]; // Y6=Y6+Y1 st Y5
        R3.H=R4-R6 (RND12) || W[P0++P3]=R5.H || R0.L=W[I2++]; // Y3=Y3-Y4 st Y1
        R3.L=R4+R6 (RND12) || W[P1++P4]=R5.L || R1.H=W[I0++]; // Y4=Y3+Y4 st Y6

        /* pipeline loop start, + drain Y3, Y4 */
        A1=R7.H*R0.H,       A0=R7.H*R0.H (IS)       || W[P0++P2]= R3.H || R1.H = W[I0--];
.1:     R3=(A1+=R7.H*R0.L), R2=(A0-=R7.H*R0.L) (IS) || W[P1++P5]= R3.L || R7 = [I3++];



    I0 = B2;                // I0 points to Input Element (0, 0)
    I2 = B2;                // I2 points to Input Element (0, 0)
    I2 += M3 || R0.H = W[I0];
                            // Y0 is read in R0.H
    I1 = I2;                // I1 points to input Element (0, 6)
    I1 += 4  || R0.L = W[I2++];
                            // I2 points to input Element (0, 4)
                            // Y4 is read in R0.L
    P2 = 8 (X);
    I3 = B3;                // I3 points to Coefficients
    P0 = B0;                // P0 points to array Element (0, 0) for writing
                            // output
    P1 = B0;
    R7 = [I3++];            // R7.H = C4 and R7.L = C4
    NOP;

    /*
     *   A1 =      Y0 * cos(pi/4)
     *   A0 =      Y0 * cos(pi/4)
     *   A1 = A1 + Y4 * cos(pi/4)
     *   A0 = A0 - Y4 * cos(pi/4)
     *   load:
     *     R1=(Y2,Y6)
     *     R7=(C2,C6)
     *   res:
     *     R3=Y0, R2=Y4
     */
    A1=R7.H*R0.H,       A0=R7.H*R0.H (IS)       || I0+=4        || R1.L=W[I1++];
    R3=(A1+=R7.H*R0.L), R2=(A0-=R7.H*R0.L) (IS) || R1.H=W[I0--] || R7=[I3++];

    LSETUP (.2, .3) LC0 = P2; // peform 8 1d idcts
    P2 = 112 (X);
    P1 = P1 + P2;
    P2 = -94(X);

.2:
        /*
         *   A1 =      Y2 * cos(3pi/8)
         *   A0 =      Y2 * cos(pi/8)
         *   A1 = A1 - Y6 * cos(pi/8)
         *   A0 = A0 + Y6 * cos(3pi/8)
         *      R5 = (Y1,Y7)
         *      R7 = (C1,C7)
         *   res:
         *      R1=Y2, R0=Y6
         */
        A1=R7.L*R1.H,       A0=R7.H*R1.H (IS)        || I0+=4        || R5.H=W[I0];
        R1=(A1-=R7.H*R1.L), R0=(A0+=R7.L*R1.L) (IS)  || R5.L=W[I1--] || R7=[I3++];
        /*
        *   Y0 = Y0 + Y6.
        *   Y4 = Y4 + Y2.
        *   Y2 = Y4 - Y2.
        *   Y6 = Y0 - Y6.
        *     R3 is saved
        *     R6.l=Y3
        * note: R3: Y0, R2: Y4, R1: Y2, R0: Y6
        */
        R3=R3+R0, R0=R3-R0;
        R2=R2+R1, R1=R2-R1 || [TMP0]=R3 || R6.L=W[I0--];
        /*
         *  Compute the odd portion (1,3,5,7) even is done.
         *
         *  Y1 = C7 * Y1 - C1 * Y7 + C3 * Y5 - C5 * Y3.
         *  Y7 = C1 * Y1 + C7 * Y7 + C5 * Y5 + C3 * Y3.
         *  Y5 = C5 * Y1 + C3 * Y7 + C7 * Y5 - C1 * Y3.
         *  Y3 = C3 * Y1 - C5 * Y7 - C1 * Y5 - C7 * Y3.
         */
        //  R5=(Y1,Y7)  R6=(Y5,Y3)                                                   // R7=(C1,C7)
        A1 =R7.L*R5.H,       A0 =R7.H*R5.H (IS)       || [TMP1]=R2 || R6.H=W[I2--];
        A1-=R7.H*R5.L,       A0+=R7.L*R5.L (IS)       || I0-=4     || R7=[I3++];
        A1+=R7.H*R6.H,       A0+=R7.L*R6.H (IS)       || I0+=M1;                     // R7=(C3,C5)
        R3 =(A1-=R7.L*R6.L), R2 =(A0+=R7.H*R6.L) (IS);
        A1 =R7.L*R5.H,       A0 =R7.H*R5.H (IS)       || R4=[TMP0];
        A1+=R7.H*R5.L,       A0-=R7.L*R5.L (IS)       || I1+=M1    || R7=[I3++];     // R7=(C1,C7)
        A1+=R7.L*R6.H,       A0-=R7.H*R6.H (IS);
        R7 =(A1-=R7.H*R6.L), R6 =(A0-=R7.L*R6.L) (IS) || I2+=M1;
        // R3=Y1, R2=Y7, R7=Y5, R6=Y3

        /* Transpose write column. */
        R5.H=R4+R2 (RND20);                                   // Y0=Y0+Y7
        R5.L=R4-R2 (RND20) || R4 = [TMP1];                    // Y7=Y7-Y0
        R5=R5>>>2(v);
        R2.H=R1+R7 (RND20) || W[P0++P3]=R5.H;                 // Y2=Y2+Y5 st Y0
        R2.L=R1-R7 (RND20) || W[P1++P4]=R5.L || R7=[I3++];    // Y5=Y2-Y5 st Y7
        R2=R2>>>2(v);
        R5.H=R0-R3 (RND20) || W[P0++P3]=R2.H || R1.L=W[I1++]; // Y1=Y6-Y1 st Y2
        R5.L=R0+R3 (RND20) || W[P1++P4]=R2.L || R0.H=W[I0++]; // Y6=Y6+Y1 st Y5
        R5=R5>>>2(v);
        R3.H=R4-R6 (RND20) || W[P0++P3]=R5.H || R0.L=W[I2++]; // Y3=Y3-Y4 st Y1
        R3.L=R4+R6 (RND20) || W[P1++P4]=R5.L || R1.H=W[I0++]; // Y4=Y3+Y4 st Y6
        R3=R3>>>2(v);
        /* pipeline loop start, + drain Y3, Y4 */
        A1=R7.H*R0.H,       A0=R7.H*R0.H (IS)       || W[P0++P2]= R3.H || R1.H = W[I0--];
.3:     R3=(A1+=R7.H*R0.L), R2=(A0-=R7.H*R0.L) (IS) || W[P1++P5]= R3.L || R7 = [I3++];

    L3 = 0;
    (R7:4,P5:3)=[SP++];
    unlink;
    RTS;
DEFUN_END(vp3_idct)
