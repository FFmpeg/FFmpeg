/*
 * fdct BlackFin
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
  void ff_bfin_fdct (DCTELEM *buf);

  This implementation works only for 8x8 input. The range of input
  must be -256 to 255 i.e. 8bit input represented in a 16bit data
  word. The original data must be sign extended into the 16bit data
  words.


   Chen factorization of

           8
   X(m) = sum (x(n) * cos ((2n+1)*m*pi/16))
          n=0

                                             C4
 0  --*-------------*0+7---*-----*0+3-------*-*-------------------> 0
       \           /        \   /            X S4,S4
 1  --*-\---------/-*1+6---*-\-/-*1+2-------*-*-------------------> 4
         \       /            \              -C4     C3
 2  --*---\-----/---*2+5---*-/-\-*1-2---------------*-*-----------> 2
           \   /            /   \                    X S3,-S3
 3  --*-----\-/-----*3+4---*-----*0-3---------------*-*-----------> 6
             /                                  C7   C3
 4  --*-----/-\-----*3-4------------*-*4+5--*-----*---------------> 1
           /   \            -C4      X       \   /S7    C3
 5  --*---/-----\---*2-5---*-*------*=*4-5----\-/------*-*--------> 5
         /       \          X S4,S4            /        X S3,-S3
 6  --*-/---------\-*1-6---*-*------*=*7-6----/-\------*-*--------> 3
       /           \        C4       X       /   \-S7   C3
    --*-------------*0-7------------*-*7+6--*-----*---------------> 7
                                                C7

Notation
        Cn = cos(n*pi/8) used throughout the code.


  Registers used:
        R0, R1, R2, R3, R4, R5, R6,R7,  P0, P1, P2, P3, P4, P5, A0, A1.
  Other registers used:
        I0, I1, I2, I3, B0, B2, B3, M0, M1, L3 registers and LC0.

  Input - r0 - pointer to start of DCTELEM *block

  Output - The DCT output coefficients in the DCTELEM *block

  Register constraint:
               This code is called from jpeg_encode.
               R6, R5, R4 if modified should be stored and restored.


  Performance: (Timer version 0.6.33)
               Code Size : 240 Bytes.
               Memory Required :
               Input Matrix : 8 * 8 * 2 Bytes.
               Coefficients : 16 Bytes
               Temporary matrix: 8 * 8 * 2 Bytes.
               Cycle Count :26+{18+8*(14+2S)}*2  where S -> Stalls
                            (7.45 c/pel)
        -----------------------------------------
        |  Size  |  Forward DCT  |  Inverse DCT |
        -----------------------------------------
        |  8x8   |   284 Cycles  |  311 Cycles  |
        -----------------------------------------

Ck = int16(cos(k/16*pi)*32767+.5)/2
#define C4 23170
#define C3 13623
#define C6 6270
#define C7 3196

Sk = int16(sin(k/16*pi)*32767+.5)/2
#define S4 11585
#define S3 9102
#define S6 15137
#define S7 16069

the coefficients are ordered as follows:
short dct_coef[]
  C4,S4,
  C6,S6,
  C7,S7,
  S3,C3,

-----------------------------------------------------------
FFMPEG conformance testing results
-----------------------------------------------------------
dct-test: modified with the following
            dct_error("BFINfdct", 0, ff_bfin_fdct, fdct, test);
produces the following output:

root:/u/ffmpeg/bhead/libavcodec> ./dct-test
ffmpeg DCT/IDCT test

    2  -131    -6   -48   -36    33   -83    24
   34    52   -24   -15     5    92    57   143
  -67   -43    -1    74   -16     5   -71    32
  -78   106    92   -34   -38    81    20   -18
    7   -62    40     2   -15    90   -62   -83
  -83     1  -104   -13    43   -19     7    11
  -63    31    12   -29    83    72    21    10
  -17   -63   -15    73    50   -91   159   -14
DCT BFINfdct: err_inf=2 err2=0.16425938 syserr=0.00795000 maxout=2098 blockSumErr=27
DCT BFINfdct: 92.1 kdct/s
root:/u/ffmpeg/bhead/libavcodec>

*/

#include "config.h"
#include "config_bfin.h"

#if defined(__FDPIC__) && CONFIG_SRAM
.section .l1.data.B,"aw",@progbits
#else
.data
#endif
.align 4;
dct_coeff:
.short 0x5a82, 0x2d41, 0x187e, 0x3b21, 0x0c7c, 0x3ec5, 0x238e, 0x3537;

#if defined(__FDPIC__) && CONFIG_SRAM
.section .l1.data.A,"aw",@progbits
#endif
.align 4
vtmp:   .space 128

.text
DEFUN(fdct,mL1,
        (DCTELEM *block)):
    [--SP] = (R7:4, P5:3);          // Push the registers onto the stack.

    b0 = r0;
    RELOC(r0, P3, dct_coeff);
    b3 = r0;
    RELOC(r0, P3, vtmp);
    b2 = r0;

    L3 = 16;                        // L3 is set to 16 to make the coefficient
                                    // array Circular.


//----------------------------------------------------------------------------

/*
 * I0, I1, and I2 registers are used to read the input data. I3 register is used
 * to read the coefficients. P0 and P1 registers are used for writing the output
 * data.
 */
    M0 = 12 (X);                    // All these initializations are used in the
    M1 = 16 (X);                    // modification of address offsets.

    M2 = 128 (X);

    P2 = 16;
    P3 = 32 (X);
    P4 = -110 (X);
    P5 = -62 (X);
    P0 = 2(X);


    // Prescale the input to get the correct precision.
    i0=b0;
    i1=b0;

    lsetup (.0, .1) LC0 = P3;
    r0=[i0++];
.0:     r1=r0<<3 (v) || r0=[i0++] ;
.1:     [i1++]=r1;

        /*
         * B0 points to the "in" buffer.
         * B2 points to "temp" buffer in the first iteration.
         */

    lsetup (.2, .3) LC0 = P0;
.2:
        I0 = B0;                        // I0 points to Input Element (0, 0).
        I1 = B0;                        // Element 1 and 0 is read in R0.
        I1 += M0  || R0 = [I0++];       // I1 points to Input Element (0, 6).
        I2 = I1;                        // Element 6 is read into R3.H.
        I2 -= 4   || R3.H = W[I1++];    // I2 points to Input Element (0, 4).

        I3 = B3;                        // I3 points to Coefficients.
        P0 = B2;                        // P0 points to temporary array Element
                                        //   (0, 0).
        P1 = B2;                        // P1 points to temporary array.
        R7 = [P1++P2] || R2 = [I2++];   // P1 points to temporary array
                                        //   Element (1, 0).
                                        // R7 is a dummy read. X4,X5
                                        //   are read into R2.
        R3.L = W[I1--];                 // X7 is read into R3.L.
        R1.H = W[I0++];                 // X2 is read into R1.H.


        /*
         *  X0 = (X0 + X7) / 2.
         *  X1 = (X1 + X6) / 2.
         *  X6 = (X1 - X6) / 2.
         *  X7 = (X0 - X7) / 2.
         *  It reads the data 3 in R1.L.
         */

        R0 = R0 +|+ R3, R3 = R0 -|- R3 || R1.L = W[I0++] || NOP;

        /*
         *       X2 = (X2 + X5) / 2.
         *       X3 = (X3 + X4) / 2.
         *       X4 = (X3 - X4) / 2.
         *       X5 = (X2 - X5) / 2.
         *          R7 = C4 = cos(4*pi/16)
         */

        R1 = R1 +|+ R2, R2 = R1 -|- R2 (CO) || NOP      ||  R7 = [I3++];

        /*
         * At the end of stage 1 R0 has (1,0), R1 has (2,3), R2 has (4, 5) and
         * R3 has (6,7).
         * Where the notation (x, y) represents uper/lower half pairs.
         */

        /*
         *       X0 = X0 + X3.
         *       X1 = X1 + X2.
         *       X2 = X1 - X2.
         *       X3 = X0 - X3.
         */
        R0 = R0 +|+ R1, R1 = R0 -|- R1;

        lsetup (.row0, .row1) LC1 = P2 >> 1;  // 1d dct, loops 8x
.row0:

        /*
         *       This is part 2 computation continued.....
         *       A1 =      X6 * cos(pi/4)
         *       A0 =      X6 * cos(pi/4)
         *       A1 = A1 - X5 * cos(pi/4)
         *       A0 = A0 + X5 * cos(pi/4).
         *       The instruction W[I0] = R3.L is used for packing it to R2.L.
         */

        A1=R3.H*R7.l,         A0=R3.H*R7.l            ||  I1+=M1 || W[I0] = R3.L;
        R4.H=(A1-=R2.L*R7.l), R4.L=(A0+=R2.L*R7.l)    ||  I2+=M0 || NOP;

        /*       R0 = (X1,X0)      R1 = (X2,X3)       R4 = (X5, X6). */

        /*
         *       A1 =      X0 * cos(pi/4)
         *       A0 =      X0 * cos(pi/4)
         *       A1 = A1 - X1 * cos(pi/4)
         *       A0 = A0 + X1 * cos(pi/4)
         *       R7 = (C2,C6)
         */
        A1=R0.L*R7.h,        A0=R0.L*R7.h             || NOP       || R3.H=W[I1++];
        R5.H=(A1-=R0.H*R7.h),R5.L=(A0+=R0.H*R7.h)     || R7=[I3++] || NOP;

        /*
         *       A1 =      X2 * cos(3pi/8)
         *       A0 =      X3 * cos(3pi/8)
         *       A1 = A1 + X3 * cos(pi/8)
         *       A0 = A0 - X2 * cos(pi/8)
         *         R3 = cos(pi/4)
         *         R7 = (cos(7pi/8),cos(pi/8))
         *       X4 = X4 + X5.
         *       X5 = X4 - X5.
         *       X6 = X7 - X6.
         *       X7 = X7 + X6.
         */
        A1=R1.H*R7.L,        A0=R1.L*R7.L             || W[P0++P3]=R5.L || R2.L=W[I0];
        R2=R2+|+R4,          R4=R2-|-R4               || I0+=4          || R3.L=W[I1--];
        R6.H=(A1+=R1.L*R7.H),R6.L=(A0 -= R1.H * R7.H) || I0+=4          || R7=[I3++];

        /*         R2 = (X4, X7)        R4 = (X5,X6)      R5 = (X1, X0)       R6 = (X2,X3). */

        /*
         *       A1 =      X4 * cos(7pi/16)
         *       A0 =      X7 * cos(7pi/16)
         *       A1 = A1 + X7 * cos(pi/16)
         *       A0 = A0 - X4 * cos(pi/16)
         */

        A1=R2.H*R7.L,        A0=R2.L*R7.L             || W[P0++P3]=R6.H || R0=[I0++];
        R2.H=(A1+=R2.L*R7.H),R2.L=(A0-=R2.H*R7.H)     || W[P0++P3]=R5.H || R7=[I3++];

        /*
         *       A1 =      X5 * cos(3pi/16)
         *       A0 =      X6 * cos(3pi/16)
         *       A1 = A1 + X6 * cos(5pi/16)
         *       A0 = A0 - X5 * cos(5pi/16)
         *       The output values are written.
         */

        A1=R4.H*R7.H,        A0=R4.L*R7.H             || W[P0++P2]=R6.L || R1.H=W[I0++];
        R4.H=(A1+=R4.L*R7.L),R4.L=(A0-=R4.H*R7.L)     || W[P0++P4]=R2.L || R1.L=W[I0++];


        /* Beginning of next stage, **pipelined** + drain and store the
           rest of the column store. */

        R0=R0+|+R3,R3=R0-|-R3                         || W[P1++P3]=R2.H || R2=[I2++];
        R1=R1+|+R2,R2=R1-|-R2 (CO)                    || W[P1++P3]=R4.L || R7=[I3++];
.row1:  R0=R0+|+R1,R1=R0-|-R1                         || W[P1++P5]=R4.H || NOP;

        // Exchange input with output.
        B1 = B0;
        B0 = B2;
.3:     B2 = B1;

        L3=0;
        (r7:4,p5:3) = [sp++];
        RTS;
DEFUN_END(fdct)
