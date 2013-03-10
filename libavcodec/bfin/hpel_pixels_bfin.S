/*
 * Blackfin Pixel Operations
 * Copyright (C) 2007 Marc Hoffman <marc.hoffman@analog.com>
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

#include "config_bfin.h"

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
DEFUN_END(put_pixels8_xy2_nornd)
