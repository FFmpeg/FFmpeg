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

POUT          .req    a1
PIN           .req    a2
PCOEF         .req    a3
DECIFACTOR    .req    a4
OLDFPSCR      .req    a4
COUNTER       .req    ip

SCALE32       .req    s28  @ use vector of 4 in place of 9th scalar when decifactor=32 / JMAX=8
SCALE64       .req    s0   @ spare register in scalar bank when decifactor=64 / JMAX=4
IN0           .req    s4
IN1           .req    s5
IN2           .req    s6
IN3           .req    s7
IN4           .req    s0
IN5           .req    s1
IN6           .req    s2
IN7           .req    s3
COEF0         .req    s8   @ coefficient elements
COEF1         .req    s9
COEF2         .req    s10
COEF3         .req    s11
COEF4         .req    s12
COEF5         .req    s13
COEF6         .req    s14
COEF7         .req    s15
ACCUM0        .req    s16  @ double-buffered multiply-accumulate results
ACCUM4        .req    s20
POST0         .req    s24  @ do long-latency post-multiply in this vector in parallel
POST1         .req    s25
POST2         .req    s26
POST3         .req    s27


.macro inner_loop  decifactor, dir, tail, head
 .ifc "\dir","up"
  .set X, 0
  .set Y, 4
 .else
  .set X, 4*JMAX*4 - 4
  .set Y, -4
 .endif
 .ifnc "\head",""
        vldr    COEF0, [PCOEF, #X + (0*JMAX + 0) * Y]
        vldr    COEF1, [PCOEF, #X + (1*JMAX + 0) * Y]
        vldr    COEF2, [PCOEF, #X + (2*JMAX + 0) * Y]
        vldr    COEF3, [PCOEF, #X + (3*JMAX + 0) * Y]
 .endif
 .ifnc "\tail",""
        vadd.f  POST0, ACCUM0, ACCUM4   @ vector operation
 .endif
 .ifnc "\head",""
        vmul.f  ACCUM0, COEF0, IN0      @ vector = vector * scalar
        vldr    COEF4, [PCOEF, #X + (0*JMAX + 1) * Y]
        vldr    COEF5, [PCOEF, #X + (1*JMAX + 1) * Y]
        vldr    COEF6, [PCOEF, #X + (2*JMAX + 1) * Y]
 .endif
 .ifnc "\tail",""
        vmul.f  POST0, POST0, SCALE\decifactor  @ vector operation (SCALE may be scalar)
 .endif
 .ifnc "\head",""
        vldr    COEF7, [PCOEF, #X + (3*JMAX + 1) * Y]
   .ifc "\tail",""
        vmul.f  ACCUM4, COEF4, IN1      @ vector operation
   .endif
        vldr    COEF0, [PCOEF, #X + (0*JMAX + 2) * Y]
        vldr    COEF1, [PCOEF, #X + (1*JMAX + 2) * Y]
   .ifnc "\tail",""
        vmul.f  ACCUM4, COEF4, IN1      @ vector operation
   .endif
        vldr    COEF2, [PCOEF, #X + (2*JMAX + 2) * Y]
        vldr    COEF3, [PCOEF, #X + (3*JMAX + 2) * Y]
 .endif
 .ifnc "\tail",""
        vstmia  POUT!, {POST0-POST3}
 .endif
 .ifnc "\head",""
        vmla.f  ACCUM0, COEF0, IN2      @ vector = vector * scalar
        vldr    COEF4, [PCOEF, #X + (0*JMAX + 3) * Y]
        vldr    COEF5, [PCOEF, #X + (1*JMAX + 3) * Y]
        vldr    COEF6, [PCOEF, #X + (2*JMAX + 3) * Y]
        vldr    COEF7, [PCOEF, #X + (3*JMAX + 3) * Y]
        vmla.f  ACCUM4, COEF4, IN3      @ vector = vector * scalar
  .if \decifactor == 32
        vldr    COEF0, [PCOEF, #X + (0*JMAX + 4) * Y]
        vldr    COEF1, [PCOEF, #X + (1*JMAX + 4) * Y]
        vldr    COEF2, [PCOEF, #X + (2*JMAX + 4) * Y]
        vldr    COEF3, [PCOEF, #X + (3*JMAX + 4) * Y]
        vmla.f  ACCUM0, COEF0, IN4      @ vector = vector * scalar
        vldr    COEF4, [PCOEF, #X + (0*JMAX + 5) * Y]
        vldr    COEF5, [PCOEF, #X + (1*JMAX + 5) * Y]
        vldr    COEF6, [PCOEF, #X + (2*JMAX + 5) * Y]
        vldr    COEF7, [PCOEF, #X + (3*JMAX + 5) * Y]
        vmla.f  ACCUM4, COEF4, IN5      @ vector = vector * scalar
        vldr    COEF0, [PCOEF, #X + (0*JMAX + 6) * Y]
        vldr    COEF1, [PCOEF, #X + (1*JMAX + 6) * Y]
        vldr    COEF2, [PCOEF, #X + (2*JMAX + 6) * Y]
        vldr    COEF3, [PCOEF, #X + (3*JMAX + 6) * Y]
        vmla.f  ACCUM0, COEF0, IN6      @ vector = vector * scalar
        vldr    COEF4, [PCOEF, #X + (0*JMAX + 7) * Y]
        vldr    COEF5, [PCOEF, #X + (1*JMAX + 7) * Y]
        vldr    COEF6, [PCOEF, #X + (2*JMAX + 7) * Y]
        vldr    COEF7, [PCOEF, #X + (3*JMAX + 7) * Y]
        vmla.f  ACCUM4, COEF4, IN7      @ vector = vector * scalar
  .endif
 .endif
.endm

.macro dca_lfe_fir  decifactor
 .if \decifactor == 32
  .set JMAX, 8
        vpush   {s16-s31}
        vmov    SCALE32, s0             @ duplicate scalar across vector
        vldr    IN4, [PIN, #-4*4]
        vldr    IN5, [PIN, #-5*4]
        vldr    IN6, [PIN, #-6*4]
        vldr    IN7, [PIN, #-7*4]
 .else
  .set JMAX, 4
        vpush   {s16-s27}
 .endif

        mov     COUNTER, #\decifactor/4 - 1
        inner_loop  \decifactor, up,, head
1:      add     PCOEF, PCOEF, #4*JMAX*4
        subs    COUNTER, COUNTER, #1
        inner_loop  \decifactor, up, tail, head
        bne     1b
        inner_loop  \decifactor, up, tail

        mov     COUNTER, #\decifactor/4 - 1
        inner_loop  \decifactor, down,, head
1:      sub     PCOEF, PCOEF, #4*JMAX*4
        subs    COUNTER, COUNTER, #1
        inner_loop  \decifactor, down, tail, head
        bne     1b
        inner_loop  \decifactor, down, tail

 .if \decifactor == 32
        vpop    {s16-s31}
 .else
        vpop    {s16-s27}
 .endif
        fmxr    FPSCR, OLDFPSCR
        bx      lr
.endm


/* void ff_dca_lfe_fir_vfp(float *out, const float *in, const float *coefs,
 *                         int decifactor, float scale)
 */
function ff_dca_lfe_fir_vfp, export=1
        teq     DECIFACTOR, #32
        fmrx    OLDFPSCR, FPSCR
        ldr     ip, =0x03030000         @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, ip
NOVFP   vldr    s0, [sp]
        vldr    IN0, [PIN, #-0*4]
        vldr    IN1, [PIN, #-1*4]
        vldr    IN2, [PIN, #-2*4]
        vldr    IN3, [PIN, #-3*4]
        beq     32f
64:     dca_lfe_fir  64
 .ltorg
32:     dca_lfe_fir  32
endfunc

        .unreq  POUT
        .unreq  PIN
        .unreq  PCOEF
        .unreq  DECIFACTOR
        .unreq  OLDFPSCR
        .unreq  COUNTER

        .unreq  SCALE32
        .unreq  SCALE64
        .unreq  IN0
        .unreq  IN1
        .unreq  IN2
        .unreq  IN3
        .unreq  IN4
        .unreq  IN5
        .unreq  IN6
        .unreq  IN7
        .unreq  COEF0
        .unreq  COEF1
        .unreq  COEF2
        .unreq  COEF3
        .unreq  COEF4
        .unreq  COEF5
        .unreq  COEF6
        .unreq  COEF7
        .unreq  ACCUM0
        .unreq  ACCUM4
        .unreq  POST0
        .unreq  POST1
        .unreq  POST2
        .unreq  POST3
