/*
 * Blackfin Pixel Operations
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
#include "config_bfin.h"

DEFUN(put_pixels_clamped,mL1,
        (DCTELEM *block, uint8_t *dest, int line_size)):
    [--SP] = (R7:4);
    R4 = 0;
    R5.l = 0x00ff;
    R5.h = 0x00ff;
    I0 = R0;         // block
    I1 = R1;         // dest
    R2 += -4;        // line_size
    M1 = R2;
    P0 = 8;
    R0 = [I0++];
    R1 = [I0++];
    R2 = MAX(R0, R4) (V);
    LSETUP (ppc$0,ppc$1) LC0=P0;
ppc$0: R2 = MIN(R2, R5) (V);
       R3 = MAX(R1, R4) (V);
       R3 = MIN(R3, R5) (V)      || R0 = [I0++];
       R6 = BYTEPACK (R2,R3)     || R1 = [I0++];
       R2 = MAX(R0, R4) (V)      || [I1++] = R6;
       R2 = MIN(R2, R5) (V);
       R3 = MAX(R1, R4) (V);
       R3 = MIN(R3, R5) (V)      || R0 = [I0++];
       R6 = BYTEPACK (R2,R3)     || R1 = [I0++];
ppc$1: R2 = Max(R0, R4) (V)      || [I1++M1] = R6;

    (R7:4) = [SP++];
    RTS;
DEFUN_END(put_pixels_clamped)

DEFUN(add_pixels_clamped,mL1,
        (DCTELEM *block, uint8_t *dest, int line_size)):
    [-- SP] = (R7:4);
    R4 = 0;
    I0 = 0;
    R2 += -4;        // line_size
    M0 = R2;
    I1 = R1;         // dest
    I3 = R0;         // block
    I2 = R1;         // dest
    P0 = 8;
    M3 = 2;
    R0 = [I3++]  || R2 = [I1];
    R2 = R2 << 8                      || R0.H = W[I3--]  || R3 = [I1++];
    R3 = R3 >> 8                      || R1.L = W[I3]    || I3 += 4;
    R6 = BYTEOP3P(R1:0, R3:2) (LO)    || R1.H = W[I3++]  || R2 = [I1];

    LSETUP(apc$2,apc$3) LC1 = P0;
apc$2: R7 = BYTEOP3P(R1:0, R3:2) (HI, R) || R0 = [I3++]     || R3 = [I1++M0];
       R2 = R2 << 8                      || R0.H = W[I3--];
       R3 = R3 >> 8                      || R1.L = W[I3]    || I3 += 4;
       R6 = R6 + R7 (S)                  || R1.H = W[I3];
       R6 = BYTEOP3P(R1:0, R3:2) (LO)    || I3+=M3          || [I2++]=R6;
       R7 = BYTEOP3P(R1:0, R3:2) (HI, R) || R0 = [I3++]     || R2 = [I1];
       R2 = R2 << 8                      || R0.H = W[I3--]  || R3 = [I1++];
       R3 = R3 >> 8                      || R1.L = W[I3]    || I3 += 4;
       R6 = R6 + R7 (S)                  || R1.H = W[I3++];
apc$3: R6 = BYTEOP3P(R1:0, R3:2) (LO)    || [I2++M0] = R6   || R2 = [I1];

    (R7:4) = [SP++];
    RTS;
DEFUN_END(add_pixels_clamped)


/*
  motion compensation
  primitives

     * Halfpel motion compensation with rounding (a+b+1)>>1.
     * This is an array[4][4] of motion compensation funcions for 4
     * horizontal blocksizes (8,16) and the 4 halfpel positions<br>
     * *pixels_tab[ 0->16xH 1->8xH ][ xhalfpel + 2*yhalfpel ]
     * @param block destination where the result is stored
     * @param pixels source
     * @param line_size number of bytes in a horizontal line of block
     * @param h height

*/

DEFUN(put_pixels8uc,mL1,
        (uint8_t *block, const uint8_t *s0, const uint8_t *s1,
                 int dest_size, int line_size, int h)):
        i3=r0;        // dest
        i0=r1;        // src0
        i1=r2;        // src1
        r0=[sp+12];   // dest_size
        r2=[sp+16];   // line_size
        p0=[sp+20];   // h
        [--sp] = (r7:6);
        r0+=-4;
        m3=r0;
        r2+=-8;
        m0=r2;
        LSETUP(pp8$0,pp8$1) LC0=P0;
        DISALGNEXCPT                || R0 = [I0++]  || R2  =[I1++];

pp8$0:  DISALGNEXCPT                || R1 = [I0++]  || R3  =[I1++];
        R6 = BYTEOP1P(R1:0,R3:2)    || R0 = [I0++M0]|| R2  =[I1++M0];
        R7 = BYTEOP1P(R1:0,R3:2)(R) || R0 = [I0++]  || [I3++] = R6 ;
pp8$1:  DISALGNEXCPT                || R2 = [I1++]  || [I3++M3] = R7;

        (r7:6) = [sp++];
        RTS;
DEFUN_END(put_pixels8uc)

DEFUN(put_pixels16uc,mL1,
        (uint8_t *block, const uint8_t *s0, const uint8_t *s1,
                 int dest_size, int line_size, int h)):
        link 0;
        [--sp] = (r7:6);
        i3=r0;        // dest
        i0=r1;        // src0
        i1=r2;        // src1
        r0=[fp+20];   // dest_size
        r2=[fp+24];   // line_size
        p0=[fp+28];   // h


        r0+=-12;
        m3=r0;        // line_size
        r2+=-16;
        m0=r2;

        LSETUP(pp16$0,pp16$1) LC0=P0;
         DISALGNEXCPT                || R0 = [I0++]   || R2  =[I1++];

pp16$0:  DISALGNEXCPT                || R1 = [I0++]   || R3  =[I1++];
         R6 = BYTEOP1P(R1:0,R3:2)    || R0 = [I0++]   || R2  =[I1++];
         R7 = BYTEOP1P(R1:0,R3:2)(R) || R1 = [I0++]   || R3  =[I1++];
         [I3++] = R6;
         R6 = BYTEOP1P(R1:0,R3:2)    || R0 = [I0++M0] || R2  =[I1++M0];
         R7 = BYTEOP1P(R1:0,R3:2)(R) || R0 = [I0++]   || [I3++] = R7 ;
         [I3++] = R6;
pp16$1:  DISALGNEXCPT                || R2 = [I1++]   || [I3++M3] = R7;

        (r7:6) = [sp++];
        unlink;
        RTS;
DEFUN_END(put_pixels16uc)






DEFUN(put_pixels8uc_nornd,mL1,
        (uint8_t *block, const uint8_t *s0, const uint8_t *s1,
                 int line_size, int h)):
        i3=r0;        // dest
        i0=r1;        // src0
        i1=r2;        // src1
        r2=[sp+12];   // line_size
        p0=[sp+16];   // h
        [--sp] = (r7:6);
        r2+=-4;
        m3=r2;
        r2+=-4;
        m0=r2;
        LSETUP(pp8$2,pp8$3) LC0=P0;
        DISALGNEXCPT                || R0 = [I0++]  || R2  =[I1++];

pp8$2:  DISALGNEXCPT                || R1 = [I0++]  || R3  =[I1++];
        R6 = BYTEOP1P(R1:0,R3:2)(T)  || R0 = [I0++M0]|| R2  =[I1++M0];
        R7 = BYTEOP1P(R1:0,R3:2)(T,R) || R0 = [I0++]  || [I3++] = R6 ;
pp8$3:  DISALGNEXCPT                || R2 = [I1++]  || [I3++M3] = R7;

        (r7:6) = [sp++];
        RTS;
DEFUN_END(put_pixels8uc_nornd)

DEFUN(put_pixels16uc_nornd,mL1,
        (uint8_t *block, const uint8_t *s0, const uint8_t *s1,
                 int line_size, int h)):
        i3=r0;        // dest
        i0=r1;        // src0
        i1=r2;        // src1
        r2=[sp+12];   // line_size
        p0=[sp+16];   // h

        [--sp] = (r7:6);
        r2+=-12;
        m3=r2;        // line_size
        r2+=-4;
        m0=r2;

        LSETUP(pp16$2,pp16$3) LC0=P0;
        DISALGNEXCPT                || R0 = [I0++]   || R2  =[I1++];

pp16$2:
        DISALGNEXCPT                || R1 = [I0++]   || R3  =[I1++];
        R6 = BYTEOP1P(R1:0,R3:2)(T)    || R0 = [I0++]   || R2  =[I1++];
        R7 = BYTEOP1P(R1:0,R3:2)(T,R) || R1 = [I0++]   || R3  =[I1++];
        [I3++] = R6;

        R6 = BYTEOP1P(R1:0,R3:2)(T)    || R0 = [I0++M0] || R2  =[I1++M0];
        R7 = BYTEOP1P(R1:0,R3:2)(T,R) || R0 = [I0++]   || [I3++] = R7 ;
        [I3++] = R6;
pp16$3: DISALGNEXCPT                || R2 = [I1++]   || [I3++M3] = R7;

        (r7:6) = [sp++];

        RTS;
DEFUN_END(put_pixels16uc_nornd)

DEFUN(z_put_pixels16_xy2,mL1,
        (uint8_t *block, const uint8_t *s0,
                 int dest_size, int line_size, int h)):
        link 0;
        [--sp] = (r7:4);
        i3=r0;        // dest
        i0=r1;        // src0--> pixels
        i1=r1;        // src1--> pixels + line_size
        r2+=-12;
        m2=r2;        // m2=dest_width-4
        r2=[fp+20];
        m3=r2;        // line_size
        p0=[fp+24];   // h
        r2+=-16;
        i1+=m3;       /* src1 + line_size */
        m0=r2;        /* line-size - 20 */

        B0 = I0;
        B1 = I1;
        B3 = I3;

        DISALGNEXCPT                       || R0 = [I0++] || R2  =[I1++];

        LSETUP(LS$16E,LE$16E) LC0=P0;
LS$16E: DISALGNEXCPT                       || R1 = [I0++] || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (RNDL)   || R0 = [I0++] || R2  =[I1++];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDL,R) || R1 = [I0++] || [I3++] = R4 ;
        DISALGNEXCPT                       || R3 = [I1++] || [I3++] = R5;
        R4 = BYTEOP2P (R3:2,R1:0) (RNDL)   || R0 = [I0++M0]|| R2  = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDL,R) || R0 = [I0++] || [I3++] = R4 ;
LE$16E: DISALGNEXCPT                       || R2 = [I1++] || [I3++M2] = R5;

        M1 = 1;
        I3 = B3;
        I1 = B1;
        I0 = B0;

        I0 += M1;
        I1 += M1;

        DISALGNEXCPT                       || R0 = [I0++] || R2  =[I1++];
        LSETUP(LS$16O,LE$16O) LC0=P0;
LS$16O: DISALGNEXCPT                       || R1 = [I0++] || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (RNDH)   || R0 = [I0++] || R2  =[I1++];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDH,R) || R1 = [I0++] || R6  =[I3++];
        R4 = R4 +|+ R6                       || R7 = [I3--];
        R5 = R5 +|+ R7                       || [I3++] = R4;
        DISALGNEXCPT                       || R3  =[I1++] || [I3++] = R5;
        R4 = BYTEOP2P (R3:2,R1:0) (RNDH)   || R0 = [I0++M0]|| R2  = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDH,R) || R0 = [I0++] || R6 = [I3++];
        R4 = R4 +|+ R6                       || R7 = [I3--];
        R5 = R5 +|+ R7                       || [I3++] = R4;
LE$16O: DISALGNEXCPT                       || R2 = [I1++] || [I3++M2] = R5;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(z_put_pixels16_xy2)

DEFUN(put_pixels16_xy2_nornd,mL1,
        (uint8_t *block, const uint8_t *s0,
                 int line_size, int h)):
        link 0;
        [--sp] = (r7:4);
        i3=r0;        // dest
        i0=r1;        // src0--> pixels
        i1=r1;        // src1--> pixels + line_size
        m3=r2;
        r2+=-12;
        m2=r2;
        r2+=-4;
        i1+=m3;       /* src1 + line_size */
        m0=r2;        /* line-size - 20 */
        p0=[fp+20];   // h

        B0=I0;
        B1=I1;
        B3=I3;

        DISALGNEXCPT                       || R0 = [I0++] || R2  =[I1++];

        LSETUP(LS$16ET,LE$16ET) LC0=P0;
LS$16ET:DISALGNEXCPT                       || R1 = [I0++] || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (TL)     || R0 = [I0++] || R2  =[I1++];
        R5 = BYTEOP2P (R3:2,R1:0) (TL,R)   || R1 = [I0++] || [I3++] = R4 ;
        DISALGNEXCPT                       || R3 = [I1++] || [I3++] = R5;
        R4 = BYTEOP2P (R3:2,R1:0) (TL)     || R0 = [I0++M0]|| R2  = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (TL,R)   || R0 = [I0++] || [I3++] = R4 ;
LE$16ET:DISALGNEXCPT                       || R2 = [I1++] || [I3++M2] = R5;

        M1 = 1;
        I3=B3;
        I1=B1;
        I0=B0;

        I0 += M1;
        I1 += M1;

        DISALGNEXCPT                       || R0 = [I0++] || R2  =[I1++];
        LSETUP(LS$16OT,LE$16OT) LC0=P0;
LS$16OT:DISALGNEXCPT                       || R1 = [I0++] || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (TH)     || R0 = [I0++] || R2  =[I1++];
        R5 = BYTEOP2P (R3:2,R1:0) (TH,R)   || R1 = [I0++] || R6  =[I3++];
        R4 = R4 +|+ R6                                    || R7 = [I3--];
        R5 = R5 +|+ R7                                    || [I3++] = R4;
        DISALGNEXCPT                       || R3  =[I1++] || [I3++] = R5;
        R4 = BYTEOP2P (R3:2,R1:0) (TH)     || R0 = [I0++M0]|| R2  = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (TH,R)   || R0 = [I0++] || R6 = [I3++];
        R4 = R4 +|+ R6                                    || R7 = [I3--];
        R5 = R5 +|+ R7                                    || [I3++] = R4;
LE$16OT:DISALGNEXCPT                       || R2 = [I1++] || [I3++M2] = R5;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(put_pixels16_xy2_nornd)

DEFUN(z_put_pixels8_xy2,mL1,
        (uint8_t *block, const uint8_t *s0,
                 int dest_size, int line_size, int h)):
        link 0;
        [--sp] = (r7:4);
        i3=r0;        // dest
        i0=r1;        // src0--> pixels
        i1=r1;        // src1--> pixels + line_size
        r2+=-4;
        m2=r2;        // m2=dest_width-4
        r2=[fp+20];
        m3=r2;        // line_size
        p0=[fp+24];   // h
        r2+=-8;
        i1+=m3;       /* src1 + line_size */
        m0=r2;        /* line-size - 20 */

        b0 = I0;
        b1 = I1;
        b3 = I3;

        LSETUP(LS$8E,LE$8E) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];
LS$8E:  DISALGNEXCPT                       || R1 = [I0++]   || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (RNDL)   || R0 = [I0++M0] || R2  =[I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDL,R) || R0 = [I0++]   || [I3++] = R4 ;
LE$8E:  DISALGNEXCPT                       || R2 = [I1++]   || [I3++M2] = R5;

        M1 = 1;
        I3 = b3;
        I1 = b1;
        I0 = b0;

        I0 += M1;
        I1 += M1;

        LSETUP(LS$8O,LE$8O) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];
LS$8O:  DISALGNEXCPT                       || R1 = [I0++]   || R3  =[I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (RNDH)   || R0 = [I0++M0] || R2  =[I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (RNDH,R) || R0 = [I0++]   || R6  =[I3++];
        R4 = R4 +|+ R6                                      || R7 = [I3--];
        R5 = R5 +|+ R7                                      || [I3++] = R4;
LE$8O:  DISALGNEXCPT                       || R2  =[I1++]   || [I3++M2] = R5;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(z_put_pixels8_xy2)

DEFUN(put_pixels8_xy2_nornd,mL1,
        (uint8_t *block, const uint8_t *s0, int line_size, int h)):
        link 0;
        [--sp] = (r7:4);
        i3=r0;        // dest
        i0=r1;        // src0--> pixels
        i1=r1;        // src1--> pixels + line_size
        m3=r2;
        r2+=-4;
        m2=r2;
        r2+=-4;
        i1+=m3;       /* src1 + line_size */
        m0=r2;        /* line-size - 20 */
        p0=[fp+20];   // h


        b0 = I0;
        b1 = I1;
        b3 = I3;

        LSETUP(LS$8ET,LE$8ET) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];

LS$8ET: DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (TL)     || R0 = [I0++M0] || R2 = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (TL,R)   || R0 = [I0++]   || [I3++] = R4 ;
LE$8ET: DISALGNEXCPT                       || R2 = [I1++]   || [I3++M2] = R5;

        M1 = 1;
        I3 = b3;
        I1 = b1;
        I0 = b0;

        I0 += M1;
        I1 += M1;

        LSETUP(LS$8OT,LE$8OT) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2 = [I1++];

LS$8OT: DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        R4 = BYTEOP2P (R3:2,R1:0) (TH)     || R0 = [I0++M0] || R2 = [I1++M0];
        R5 = BYTEOP2P (R3:2,R1:0) (TH,R)   || R0 = [I0++]   || R6 = [I3++];
        R4 = R4 +|+ R6                                      || R7 = [I3--];
        R5 = R5 +|+ R7                                      || [I3++] = R4;
LE$8OT: DISALGNEXCPT                       || R2  =[I1++]   || [I3++M2] = R5;

        (r7:4) = [sp++];
        unlink;
        rts;

DEFUN(diff_pixels,mL1,
       (DCTELEM *block, uint8_t *s1, uint8_t *s2, int stride)):
        link 0;
        [--sp] = (r7:4);
        p0=8;
        i3=r0;        // block
        i0=r1;        // s1
        i1=r2;        // s2
        r2=[fp+20];   // stride
        r2+=-8;
        m0=r2;


        LSETUP(.LS0,.LE0) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];

.LS0:   DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        (R5,R4) = BYTEOP16M (R1:0,R3:2)    || R0 = [I0++M0] || R2 = [I1++M0];
        (R7,R6) = BYTEOP16M (R1:0,R3:2) (R)|| R0 = [I0++]   || [I3++] = R4;
        DISALGNEXCPT                       || R2 = [I1++]   || [I3++] = R5;
        [i3++]=r6;
.LE0:  [i3++]=r7;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(put_pixels8_xy2_nornd)

/*
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
          sum += pix[j];
        }
        pix += line_size;
    }
*/
DEFUN(pix_sum,mL1,
        (uint8_t *p, int stride)):
        link 0;
        [--sp] = (r7:4);
        p0=8;
        i0=r0;        // s1
        i1=r0;
        m1=r1;
        r1=r1+r1;
        r1+=-16;       // stride
        m0=r1;
        i1+=m1;

        r6=0;

        LSETUP(LS$PS,LE$PS) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];

LS$PS:  DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        (R5,R4) = BYTEOP16P (R3:2,R1:0)    || R0 = [I0++]   || R2 = [I1++];
        r6=r6+|+r5;
        r6=r6+|+r4;
        (R5,R4) = BYTEOP16P (R3:2,R1:0) (R)|| R1 = [I0++]   || R3 = [I1++];
        r6=r6+|+r5;
        r6=r6+|+r4;
        (R5,R4) = BYTEOP16P (R3:2,R1:0)    || R0 = [I0++m0] || R2 = [I1++m0];
        r6=r6+|+r5;
        r6=r6+|+r4;
        (R5,R4) = BYTEOP16P (R3:2,R1:0) (R)|| R0 = [I0++]   || R2 = [I1++];
        r6=r6+|+r5;
LE$PS:  r6=r6+|+r4;
        r0.l=r6.l+r6.h;
        r0.h=0;

        (r7:4) = [sp++];
        unlink;
        rts;
DEFUN_END(pix_sum)


DEFUN(get_pixels,mL1,
        (DCTELEM *av_restrict block, const uint8_t *pixels, int line_size)):
        [--sp] = (r7:4);
        i3=r0;        // dest
        i0=r1;        // src0
        p0=8;
        r2+=-8;
        m0=r2;
        LSETUP(gp8$0,gp8$1) LC0=P0;

        DISALGNEXCPT                   || R0 = [I0++];
        DISALGNEXCPT                   || R1 = [I0++];

gp8$0:  (R7,R6) = byteunpack R1:0      || R0 = [I0++M0];
        (R5,R4) = byteunpack R1:0 (R)  || R0 = [I0++]    || [I3++]=R6;
        DISALGNEXCPT                   || R1 = [I0++]    || [I3++]=R7;
        [I3++]=R4;
gp8$1:  [I3++]=R5


        (r7:4) = [sp++];
        RTS;
DEFUN_END(get_pixels)


/* sad = sad16x16 (ubyte *mb, ubyte *refwin, srcwidth, refwinwidth, h) */
/* 91 cycles */
DEFUN(z_sad16x16,mL1,
        (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h)):
        link 0;
        I0 = R0;
        I1 = R1;

        A1 = A0 = 0;
        R0 = [sp+20]; // rwidth
        P2 = [sp+24]; // height
        R3 = 16;
        R0 = R0 - R3;
        R3 = R2 - R3;
        M1 = R0;
        M0 = R3;

        DISALGNEXCPT         || R0 = [I0++]    || R2 = [I1++];
        LSETUP (s$16, e$16) LC0=P2;
s$16:   DISALGNEXCPT         || R1 = [I0++]    || R3 = [I1++];
        SAA (R1:0,R3:2)      || R0 = [I0++]    || R2 = [I1++];
        SAA (R1:0,R3:2) (R)  || R1 = [I0++]    || R3 = [I1++];
        SAA (R1:0,R3:2)      || R0 = [I0++M0]  || R2 = [I1++M1];
e$16:   SAA (R1:0,R3:2) (R)  || R0 = [I0++]    || R2 = [I1++];

        R3=A1.L+A1.H,  R2=A0.L+A0.H ;
        R0 = R2 + R3 ;
        unlink;
        RTS;
DEFUN_END(z_sad16x16)

/* sad = sad8x8 (ubyte *mb, ubyte *refwin, int srcwidth, int refwinwidth, int h) */
/* 36 cycles */
DEFUN(z_sad8x8,mL1,
        (uint8_t *blk1, uint8_t *blk2, int dsz, int line_size, int h)):
        I0 = R0;
        I1 = R1;

        A1 = A0 = 0;
        r0 = [sp+12]; // rwidth
        P2 = [sp+16]; //height
        R3 = 8;
        R0 = R0 - R3;
        R3 = R2 - R3;
        M0 = R3;
        M1 = R0;

        LSETUP (s$8, e$8) LC0=P2;
        DISALGNEXCPT         || R0 = [I0++]   || R2 = [I1++];
        DISALGNEXCPT         || R1 = [I0++]   || R3 = [I1++];
s$8:    SAA (R1:0,R3:2)      || R0 = [I0++M0] || R2 = [I1++M1];
        SAA (R1:0,R3:2) (R)  || R0 = [I0++]   || R2 = [I1++];
e$8:    DISALGNEXCPT         || R1 = [I0++]   || R3 = [I1++];

        R3=A1.L+A1.H,  R2=A0.L+A0.H ;
        R0 = R2 + R3 ;
        RTS;
DEFUN_END(z_sad8x8)

DEFUN(pix_norm1,mL1,
        (uint8_t * pix, int line_size)):
        [--SP]=(R7:4,P5:3);

        // Fetch the input arguments.
        P1 = R0;  // pix
        P0 = R1;  // line_size
        P5 = 16;  // loop ctr.
        P0 -= P5;
        M0 = P0;  // M0 = line_size-16;
        // Now for the real work.
        A1 = A0 = 0;
        lsetup(_pix_norm1_blkfn_loopStart, _pix_norm1_blkfn_loopEnd) LC1 = P5;
        I0 = P1;
        DISALGNEXCPT || r0 = [i0++];

_pix_norm1_blkfn_loopStart:
        // following unpacks pix1[0..15] pix1+line_size[0..15]
        DISALGNEXCPT || r1 = [i0++];

        (r5, r4) = byteunpack r1:0 || r0 = [i0++];
        a1 += r5.h * r5.h, a0 += r5.l * r5.l (is);
        a1 += r4.h * r4.h, a0 += r4.l * r4.l (is);
        (r5, r4) = byteunpack r1:0(r) || r1 = [i0++];
        a1 += r5.h * r5.h, a0 += r5.l * r5.l (is);
        a1 += r4.h * r4.h, a0 += r4.l * r4.l (is);
        (r5, r4) = byteunpack r1:0 || r0 = [i0++M0];
        a1 += r5.h * r5.h, a0 += r5.l * r5.l (is);
        a1 += r4.h * r4.h, a0 += r4.l * r4.l (is);
        (r5, r4) = byteunpack r1:0(r) || r0 = [i0++];
        a1 += r5.h * r5.h, a0 += r5.l * r5.l (is);
_pix_norm1_blkfn_loopEnd:
        a1 += r4.h * r4.h, a0 += r4.l * r4.l (is);


// Clean up at the end:
        R2 = A0, R3 = A1;
        R0 = R2 + R3 (S);

        (R7:4,P5:3)=[SP++];

        RTS;
DEFUN_END(pix_norm1)

DEFUN(sse4,mL1,
        (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)):
        link 0;
        [--sp] = (r7:6);
        p0=[fp+24];   // h
        i0=r1;        // pix1
        i1=r2;        // pix2
        r2=[fp+20];   // line_size
        r2+=-4;
        m0=r2;

        a0=a1=0;
        LSETUP(.S40,.E40) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];

.S40:   DISALGNEXCPT                       || R1 = [I0++M0] || R3 = [I1++M0];
        (R7,R6) = BYTEOP16M (R1:0,R3:2);
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
.E40:   a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        a0 += a1;
        r0 = a0;

        (r7:6) = [sp++];
        unlink;
        rts;
DEFUN_END(sse4)

DEFUN(sse8,mL1,
        (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)):
        link 0;
        [--sp] = (r7:6);
        p0=[fp+24];   // h
        i0=r1;        // pix1
        i1=r2;        // pix2
        r2=[fp+20];   // line_size
        r2+=-8;
        m0=r2;

        a0=a1=0;
        LSETUP(.S80,.E80) LC0=P0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];

.S80:   DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        (R7,R6) = BYTEOP16M (R1:0,R3:2)    || R0 = [I0++M0] || R2 = [I1++M0];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
        a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        (R7,R6) = BYTEOP16M (R1:0,R3:2) (R)|| R0 = [I0++]   || R2 = [I1++];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
.E80:   a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        a0 += a1;
        r0 = a0;

        (r7:6) = [sp++];
        unlink;
        rts;
DEFUN_END(sse8)

DEFUN(sse16,mL1,
        (void *v, uint8_t *pix1, uint8_t *pix2, int line_size, int h)):
        link 0;
        [--sp] = (r7:6);
        p0=[fp+24];   // h
        i0=r1;        // pix1
        i1=r2;        // pix2
        r2=[fp+20];   // line_size
        r2+=-16;
        m0=r2;

        a0=a1=0;
        DISALGNEXCPT                       || R0 = [I0++]   || R2  =[I1++];
        LSETUP(.S160,.E160) LC0=P0;

.S160:  DISALGNEXCPT                       || R1 = [I0++]   || R3 = [I1++];
        (R7,R6) = BYTEOP16M (R1:0,R3:2)    || R0 = [I0++]   || R2 = [I1++];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
        a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        (R7,R6) = BYTEOP16M (R1:0,R3:2) (R)|| R1 = [I0++]   || R3 = [I1++];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
        a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        (R7,R6) = BYTEOP16M (R1:0,R3:2)    || R0 = [I0++M0] || R2 = [I1++M0];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
        a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        (R7,R6) = BYTEOP16M (R1:0,R3:2) (R)|| R0 = [I0++]   || R2 = [I1++];
        a0 += r7.l * r7.l, a1 += r7.h * r7.h (is);
.E160:  a0 += r6.l * r6.l, a1 += r6.h * r6.h (is);
        a0 += a1;
        r0 = a0;

        (r7:6) = [sp++];
        unlink;
        rts;
DEFUN_END(sse16)
