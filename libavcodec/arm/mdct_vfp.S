/*
 * Copyright (c) 2013 RISC OS Open Ltd
 * Author: Ben Avison <bavison@riscosopen.org>
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

#include "libavutil/arm/asm.S"

CONTEXT .req    a1
ORIGOUT .req    a2
IN      .req    a3
OUT     .req    v1
REVTAB  .req    v2
TCOS    .req    v3
TSIN    .req    v4
OLDFPSCR .req   v5
J0      .req    a2
J1      .req    a4
J2      .req    ip
J3      .req    lr

.macro prerotation_innerloop
 .set trig_lo, k
 .set trig_hi, n4 - k - 2
 .set in_lo, trig_lo * 2
 .set in_hi, trig_hi * 2
        vldr    d8, [TCOS, #trig_lo*4]          @ s16,s17
        vldr    d9, [TCOS, #trig_hi*4]          @ s18,s19
        vldr    s0, [IN, #in_hi*4 + 12]
        vldr    s1, [IN, #in_hi*4 + 4]
        vldr    s2, [IN, #in_lo*4 + 12]
        vldr    s3, [IN, #in_lo*4 + 4]
        vmul.f  s8, s0, s16                     @ vector operation
        vldr    d10, [TSIN, #trig_lo*4]         @ s20,s21
        vldr    d11, [TSIN, #trig_hi*4]         @ s22,s23
        vldr    s4, [IN, #in_lo*4]
        vldr    s5, [IN, #in_lo*4 + 8]
        vldr    s6, [IN, #in_hi*4]
        vldr    s7, [IN, #in_hi*4 + 8]
        ldr     J0, [REVTAB, #trig_lo*2]
        vmul.f  s12, s0, s20                    @ vector operation
        ldr     J2, [REVTAB, #trig_hi*2]
        mov     J1, J0, lsr #16
        and     J0, J0, #255                    @ halfword value will be < n4
        vmls.f  s8, s4, s20                     @ vector operation
        mov     J3, J2, lsr #16
        and     J2, J2, #255                    @ halfword value will be < n4
        add     J0, OUT, J0, lsl #3
        vmla.f  s12, s4, s16                    @ vector operation
        add     J1, OUT, J1, lsl #3
        add     J2, OUT, J2, lsl #3
        add     J3, OUT, J3, lsl #3
        vstr    s8, [J0]
        vstr    s9, [J1]
        vstr    s10, [J2]
        vstr    s11, [J3]
        vstr    s12, [J0, #4]
        vstr    s13, [J1, #4]
        vstr    s14, [J2, #4]
        vstr    s15, [J3, #4]
 .set k, k + 2
.endm

.macro postrotation_innerloop tail, head
 .set trig_lo_head, n8 - k - 2
 .set trig_hi_head, n8 + k
 .set out_lo_head, trig_lo_head * 2
 .set out_hi_head, trig_hi_head * 2
 .set trig_lo_tail, n8 - (k - 2) - 2
 .set trig_hi_tail, n8 + (k - 2)
 .set out_lo_tail, trig_lo_tail * 2
 .set out_hi_tail, trig_hi_tail * 2
 .if (k & 2) == 0
  TCOS_D0_HEAD .req d10 @ s20,s21
  TCOS_D1_HEAD .req d11 @ s22,s23
  TCOS_S0_TAIL .req s24
 .else
  TCOS_D0_HEAD .req d12 @ s24,s25
  TCOS_D1_HEAD .req d13 @ s26,s27
  TCOS_S0_TAIL .req s20
 .endif
 .ifnc "\tail",""
        vmls.f  s8, s0, TCOS_S0_TAIL        @ vector operation
 .endif
 .ifnc "\head",""
        vldr    d8, [TSIN, #trig_lo_head*4] @ s16,s17
        vldr    d9, [TSIN, #trig_hi_head*4] @ s18,s19
        vldr    TCOS_D0_HEAD, [TCOS, #trig_lo_head*4]
 .endif
 .ifnc "\tail",""
        vmla.f  s12, s4, TCOS_S0_TAIL       @ vector operation
 .endif
 .ifnc "\head",""
        vldr    s0, [OUT, #out_lo_head*4]
        vldr    s1, [OUT, #out_lo_head*4 + 8]
        vldr    s2, [OUT, #out_hi_head*4]
        vldr    s3, [OUT, #out_hi_head*4 + 8]
        vldr    s4, [OUT, #out_lo_head*4 + 4]
        vldr    s5, [OUT, #out_lo_head*4 + 12]
        vldr    s6, [OUT, #out_hi_head*4 + 4]
        vldr    s7, [OUT, #out_hi_head*4 + 12]
 .endif
 .ifnc "\tail",""
        vstr    s8, [OUT, #out_lo_tail*4]
        vstr    s9, [OUT, #out_lo_tail*4 + 8]
        vstr    s10, [OUT, #out_hi_tail*4]
        vstr    s11, [OUT, #out_hi_tail*4 + 8]
 .endif
 .ifnc "\head",""
        vmul.f  s8, s4, s16                 @ vector operation
 .endif
 .ifnc "\tail",""
        vstr    s12, [OUT, #out_hi_tail*4 + 12]
        vstr    s13, [OUT, #out_hi_tail*4 + 4]
        vstr    s14, [OUT, #out_lo_tail*4 + 12]
        vstr    s15, [OUT, #out_lo_tail*4 + 4]
 .endif
 .ifnc "\head",""
        vmul.f  s12, s0, s16                @ vector operation
        vldr    TCOS_D1_HEAD, [TCOS, #trig_hi_head*4]
 .endif
 .unreq TCOS_D0_HEAD
 .unreq TCOS_D1_HEAD
 .unreq TCOS_S0_TAIL
 .ifnc "\head",""
  .set k, k + 2
 .endif
.endm


/* void ff_imdct_half_vfp(FFTContext *s,
 *                        FFTSample *output,
 *                        const FFTSample *input)
 */
function ff_imdct_half_vfp, export=1
        ldr     ip, [CONTEXT, #5*4]         @ mdct_bits
        teq     ip, #6
        it      ne
        bne     X(ff_imdct_half_c)          @ only case currently accelerated is the one used by DCA

 .set n, 1<<6
 .set n2, n/2
 .set n4, n/4
 .set n8, n/8

        push    {v1-v5,lr}
        vpush   {s16-s27}
        fmrx    OLDFPSCR, FPSCR
        ldr     lr, =0x03030000             @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, lr
        mov     OUT, ORIGOUT
        ldr     REVTAB, [CONTEXT, #2*4]
        ldr     TCOS, [CONTEXT, #6*4]
        ldr     TSIN, [CONTEXT, #7*4]

 .set k, 0
 .rept n8/2
        prerotation_innerloop
 .endr

        fmxr    FPSCR, OLDFPSCR
        mov     a1, OUT
        bl      X(ff_fft16_vfp)
        ldr     lr, =0x03030000             @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, lr

 .set k, 0
        postrotation_innerloop , head
 .rept n8/2 - 1
        postrotation_innerloop tail, head
 .endr
        postrotation_innerloop tail

        fmxr    FPSCR, OLDFPSCR
        vpop    {s16-s27}
        pop     {v1-v5,pc}
endfunc

        .unreq  CONTEXT
        .unreq  ORIGOUT
        .unreq  IN
        .unreq  OUT
        .unreq  REVTAB
        .unreq  TCOS
        .unreq  TSIN
        .unreq  OLDFPSCR
        .unreq  J0
        .unreq  J1
        .unreq  J2
        .unreq  J3
