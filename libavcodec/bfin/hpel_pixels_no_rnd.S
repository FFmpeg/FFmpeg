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

#include "libavutil/bfin/asm.h"

DEFUN(put_pixels8uc_no_rnd,mL1,
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
DEFUN_END(put_pixels8uc_no_rnd)

DEFUN(put_pixels16uc_no_rnd,mL1,
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
DEFUN_END(put_pixels16uc_no_rnd)
