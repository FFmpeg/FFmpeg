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

IMDCT         .req    r0
ORIG_P_SB     .req    r1
P_SB_OFF      .req    r2
I             .req    r0
P_SB2_UP      .req    r1
OLDFPSCR      .req    r2
P_SB2_DN      .req    r3
P_WIN_DN      .req    r4
P_OUT_DN      .req    r5
P_SB          .req    r6
J_WRAP        .req    r7
P_WIN_UP      .req    r12
P_OUT_UP      .req    r14

SCALE         .req    s0
SBUF_DAT_REV0 .req    s4
SBUF_DAT_REV1 .req    s5
SBUF_DAT_REV2 .req    s6
SBUF_DAT_REV3 .req    s7
VA0           .req    s8
VA3           .req    s11
VB0           .req    s12
VB3           .req    s15
VC0           .req    s8
VC3           .req    s11
VD0           .req    s12
VD3           .req    s15
SBUF_DAT0     .req    s16
SBUF_DAT1     .req    s17
SBUF_DAT2     .req    s18
SBUF_DAT3     .req    s19
SBUF_DAT_ALT0 .req    s20
SBUF_DAT_ALT1 .req    s21
SBUF_DAT_ALT2 .req    s22
SBUF_DAT_ALT3 .req    s23
WIN_DN_DAT0   .req    s24
WIN_UP_DAT0   .req    s28


.macro inner_loop  half, tail, head
 .if (OFFSET & (64*4)) == 0                @ even numbered call
        SBUF_DAT_THIS0 .req SBUF_DAT0
        SBUF_DAT_THIS1 .req SBUF_DAT1
        SBUF_DAT_THIS2 .req SBUF_DAT2
        SBUF_DAT_THIS3 .req SBUF_DAT3
  .ifnc "\head",""
        vldr    d8, [P_SB, #OFFSET]        @ d8 = SBUF_DAT
        vldr    d9, [P_SB, #OFFSET+8]
  .endif
 .else
        SBUF_DAT_THIS0 .req SBUF_DAT_ALT0
        SBUF_DAT_THIS1 .req SBUF_DAT_ALT1
        SBUF_DAT_THIS2 .req SBUF_DAT_ALT2
        SBUF_DAT_THIS3 .req SBUF_DAT_ALT3
  .ifnc "\head",""
        vldr    d10, [P_SB, #OFFSET]       @ d10 = SBUF_DAT_ALT
        vldr    d11, [P_SB, #OFFSET+8]
  .endif
 .endif
 .ifnc "\tail",""
  .ifc "\half","ab"
        vmls.f  VA0, SBUF_DAT_REV0, WIN_DN_DAT0  @ all operands treated as vectors
  .else
        vmla.f  VD0, SBUF_DAT_REV0, WIN_DN_DAT0  @ all operands treated as vectors
  .endif
 .endif
 .ifnc "\head",""
        vldr    d14, [P_WIN_UP, #OFFSET]   @ d14 = WIN_UP_DAT
        vldr    d15, [P_WIN_UP, #OFFSET+8]
        vldr    d12, [P_WIN_DN, #OFFSET]   @ d12 = WIN_DN_DAT
        vldr    d13, [P_WIN_DN, #OFFSET+8]
        vmov    SBUF_DAT_REV3, SBUF_DAT_THIS0
        vmov    SBUF_DAT_REV2, SBUF_DAT_THIS1
        vmov    SBUF_DAT_REV1, SBUF_DAT_THIS2
        vmov    SBUF_DAT_REV0, SBUF_DAT_THIS3
  .ifc "\half","ab"
        vmla.f  VB0, SBUF_DAT_THIS0, WIN_UP_DAT0
  .else
        vmla.f  VC0, SBUF_DAT_THIS0, WIN_UP_DAT0
  .endif
        teq     J_WRAP, #J
        bne     2f             @ strongly predictable, so better than cond exec in this case
        sub     P_SB, P_SB, #512*4
2:
  .set J, J - 64
  .set OFFSET, OFFSET + 64*4
 .endif
        .unreq  SBUF_DAT_THIS0
        .unreq  SBUF_DAT_THIS1
        .unreq  SBUF_DAT_THIS2
        .unreq  SBUF_DAT_THIS3
.endm


/* void ff_synth_filter_float_vfp(FFTContext *imdct,
 *                                float *synth_buf_ptr, int *synth_buf_offset,
 *                                float synth_buf2[32], const float window[512],
 *                                float out[32], const float in[32], float scale)
 */
function ff_synth_filter_float_vfp, export=1
        push    {r3-r7,lr}
        vpush   {s16-s31}
        ldr     lr, [P_SB_OFF]
        add     a2, ORIG_P_SB, lr, lsl #2 @ calculate synth_buf to pass to imdct_half
        mov     P_SB, a2                  @ and keep a copy for ourselves
        bic     J_WRAP, lr, #63           @ mangled to make testing for wrap easier in inner loop
        sub     lr, lr, #32
        and     lr, lr, #512-32
        str     lr, [P_SB_OFF]            @ rotate offset, modulo buffer size, ready for next call
        ldr     a3, [sp, #(16+6+2)*4]     @ fetch in from stack, to pass to imdct_half
        mov     a4, #4                    @ sizeof(float)
VFP     ldr     r12, [sp, #(16+6+3)*4]    @ fetch imdct_fn from stack
NOVFP   ldr     r12, [sp, #(16+6+4)*4]    @ fetch imdct_fn from stack
VFP     vmov    s16, SCALE                @ imdct_half is free to corrupt s0, but it contains one of our arguments in hardfp case
        blx     r12
VFP     vmov    SCALE, s16

        fmrx    OLDFPSCR, FPSCR
        ldr     lr, =0x03030000           @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, lr
        ldr     P_SB2_DN, [sp, #16*4]
        ldr     P_WIN_DN, [sp, #(16+6+0)*4]
        ldr     P_OUT_DN, [sp, #(16+6+1)*4]
NOVFP   vldr    SCALE, [sp, #(16+6+3)*4]

#define IMM_OFF_SKEW 956                   /* also valid immediate constant when you add 16*4 */
        add     P_SB, P_SB, #IMM_OFF_SKEW  @ so we can use -ve offsets to use full immediate offset range
        add     P_SB2_UP, P_SB2_DN, #16*4
        add     P_WIN_UP, P_WIN_DN, #16*4+IMM_OFF_SKEW
        add     P_OUT_UP, P_OUT_DN, #16*4
        add     P_SB2_DN, P_SB2_DN, #16*4
        add     P_WIN_DN, P_WIN_DN, #12*4+IMM_OFF_SKEW
        add     P_OUT_DN, P_OUT_DN, #16*4
        mov     I, #4
1:
        vldmia  P_SB2_UP!, {VB0-VB3}
        vldmdb  P_SB2_DN!, {VA0-VA3}
 .set J, 512 - 64
 .set OFFSET, -IMM_OFF_SKEW
        inner_loop  ab,, head
 .rept 7
        inner_loop  ab, tail, head
 .endr
        inner_loop  ab, tail
        add     P_WIN_UP, P_WIN_UP, #4*4
        sub     P_WIN_DN, P_WIN_DN, #4*4
        vmul.f  VB0, VB0, SCALE      @ SCALE treated as scalar
        add     P_SB, P_SB, #(512+4)*4
        subs    I, I, #1
        vmul.f  VA0, VA0, SCALE
        vstmia  P_OUT_UP!, {VB0-VB3}
        vstmdb  P_OUT_DN!, {VA0-VA3}
        bne     1b

        add     P_SB2_DN, P_SB2_DN, #(16+28-12)*4
        sub     P_SB2_UP, P_SB2_UP, #(16+16)*4
        add     P_WIN_DN, P_WIN_DN, #(32+16+28-12)*4
        mov     I, #4
1:
        vldr.d  d4, zero             @ d4 = VC0
        vldr.d  d5, zero
        vldr.d  d6, zero             @ d6 = VD0
        vldr.d  d7, zero
 .set J, 512 - 64
 .set OFFSET, -IMM_OFF_SKEW
        inner_loop  cd,, head
 .rept 7
        inner_loop  cd, tail, head
 .endr
        inner_loop  cd, tail
        add     P_WIN_UP, P_WIN_UP, #4*4
        sub     P_WIN_DN, P_WIN_DN, #4*4
        add     P_SB, P_SB, #(512+4)*4
        subs    I, I, #1
        vstmia  P_SB2_UP!, {VC0-VC3}
        vstmdb  P_SB2_DN!, {VD0-VD3}
        bne     1b

        fmxr    FPSCR, OLDFPSCR
        vpop    {s16-s31}
        pop     {r3-r7,pc}
endfunc

        .unreq  IMDCT
        .unreq  ORIG_P_SB
        .unreq  P_SB_OFF
        .unreq  I
        .unreq  P_SB2_UP
        .unreq  OLDFPSCR
        .unreq  P_SB2_DN
        .unreq  P_WIN_DN
        .unreq  P_OUT_DN
        .unreq  P_SB
        .unreq  J_WRAP
        .unreq  P_WIN_UP
        .unreq  P_OUT_UP

        .unreq  SCALE
        .unreq  SBUF_DAT_REV0
        .unreq  SBUF_DAT_REV1
        .unreq  SBUF_DAT_REV2
        .unreq  SBUF_DAT_REV3
        .unreq  VA0
        .unreq  VA3
        .unreq  VB0
        .unreq  VB3
        .unreq  VC0
        .unreq  VC3
        .unreq  VD0
        .unreq  VD3
        .unreq  SBUF_DAT0
        .unreq  SBUF_DAT1
        .unreq  SBUF_DAT2
        .unreq  SBUF_DAT3
        .unreq  SBUF_DAT_ALT0
        .unreq  SBUF_DAT_ALT1
        .unreq  SBUF_DAT_ALT2
        .unreq  SBUF_DAT_ALT3
        .unreq  WIN_DN_DAT0
        .unreq  WIN_UP_DAT0

        .align  3
zero:   .word   0, 0
