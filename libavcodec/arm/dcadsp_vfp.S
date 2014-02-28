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

POUT          .req    a1
PIN           .req    a2
PCOEF         .req    a3
OLDFPSCR      .req    a4
COUNTER       .req    ip

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
function ff_dca_lfe_fir\decifactor\()_vfp, export=1
        fmrx    OLDFPSCR, FPSCR
        ldr     ip, =0x03030000         @ RunFast mode, short vectors of length 4, stride 1
        fmxr    FPSCR, ip
        vldr    IN0, [PIN, #-0*4]
        vldr    IN1, [PIN, #-1*4]
        vldr    IN2, [PIN, #-2*4]
        vldr    IN3, [PIN, #-3*4]
 .if \decifactor == 32
  .set JMAX, 8
        vpush   {s16-s31}
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
endfunc
.endm

        dca_lfe_fir  64
 .ltorg
        dca_lfe_fir  32

        .unreq  POUT
        .unreq  PIN
        .unreq  PCOEF
        .unreq  OLDFPSCR
        .unreq  COUNTER

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


IN      .req    a1
SBACT   .req    a2
OLDFPSCR .req   a3
IMDCT   .req    a4
WINDOW  .req    v1
OUT     .req    v2
BUF     .req    v3
SCALEINT .req   v4 @ only used in softfp case
COUNT   .req    v5

SCALE   .req    s0

/* Stack layout differs in softfp and hardfp cases:
 *
 * hardfp
 *      fp -> 6 arg words saved by caller
 *            a3,a4,v1-v3,v5,fp,lr on entry (a3 just to pad to 8 bytes)
 *            s16-s23 on entry
 *            align 16
 *     buf -> 8*32*4 bytes buffer
 *            s0 on entry
 *      sp -> 3 arg words for callee
 *
 * softfp
 *      fp -> 7 arg words saved by caller
 *            a4,v1-v5,fp,lr on entry
 *            s16-s23 on entry
 *            align 16
 *     buf -> 8*32*4 bytes buffer
 *      sp -> 4 arg words for callee
 */

/* void ff_dca_qmf_32_subbands_vfp(float samples_in[32][8], int sb_act,
 *                                 SynthFilterContext *synth, FFTContext *imdct,
 *                                 float (*synth_buf_ptr)[512],
 *                                 int *synth_buf_offset, float (*synth_buf2)[32],
 *                                 const float (*window)[512], float *samples_out,
 *                                 float (*raXin)[32], float scale);
 */
function ff_dca_qmf_32_subbands_vfp, export=1
VFP     push    {a3-a4,v1-v3,v5,fp,lr}
NOVFP   push    {a4,v1-v5,fp,lr}
        add     fp, sp, #8*4
        vpush   {s16-s23}
        @ The buffer pointed at by raXin isn't big enough for us to do a
        @ complete matrix transposition as we want to, so allocate an
        @ alternative buffer from the stack. Align to 4 words for speed.
        sub     BUF, sp, #8*32*4
        bic     BUF, BUF, #15
        mov     sp, BUF
        ldr     lr, =0x03330000     @ RunFast mode, short vectors of length 4, stride 2
        fmrx    OLDFPSCR, FPSCR
        fmxr    FPSCR, lr
        @ COUNT is used to count down 2 things at once:
        @ bits 0-4 are the number of word pairs remaining in the output row
        @ bits 5-31 are the number of words to copy (with possible negation)
        @   from the source matrix before we start zeroing the remainder
        mov     COUNT, #(-4 << 5) + 16
        adds    COUNT, COUNT, SBACT, lsl #5
        bmi     2f
1:
        vldr    s8,  [IN, #(0*8+0)*4]
        vldr    s10, [IN, #(0*8+1)*4]
        vldr    s12, [IN, #(0*8+2)*4]
        vldr    s14, [IN, #(0*8+3)*4]
        vldr    s16, [IN, #(0*8+4)*4]
        vldr    s18, [IN, #(0*8+5)*4]
        vldr    s20, [IN, #(0*8+6)*4]
        vldr    s22, [IN, #(0*8+7)*4]
        vneg.f  s8, s8
        vldr    s9,  [IN, #(1*8+0)*4]
        vldr    s11, [IN, #(1*8+1)*4]
        vldr    s13, [IN, #(1*8+2)*4]
        vldr    s15, [IN, #(1*8+3)*4]
        vneg.f  s16, s16
        vldr    s17, [IN, #(1*8+4)*4]
        vldr    s19, [IN, #(1*8+5)*4]
        vldr    s21, [IN, #(1*8+6)*4]
        vldr    s23, [IN, #(1*8+7)*4]
        vstr    d4,  [BUF, #(0*32+0)*4]
        vstr    d5,  [BUF, #(1*32+0)*4]
        vstr    d6,  [BUF, #(2*32+0)*4]
        vstr    d7,  [BUF, #(3*32+0)*4]
        vstr    d8,  [BUF, #(4*32+0)*4]
        vstr    d9,  [BUF, #(5*32+0)*4]
        vstr    d10, [BUF, #(6*32+0)*4]
        vstr    d11, [BUF, #(7*32+0)*4]
        vldr    s9,  [IN, #(3*8+0)*4]
        vldr    s11, [IN, #(3*8+1)*4]
        vldr    s13, [IN, #(3*8+2)*4]
        vldr    s15, [IN, #(3*8+3)*4]
        vldr    s17, [IN, #(3*8+4)*4]
        vldr    s19, [IN, #(3*8+5)*4]
        vldr    s21, [IN, #(3*8+6)*4]
        vldr    s23, [IN, #(3*8+7)*4]
        vneg.f  s9, s9
        vldr    s8,  [IN, #(2*8+0)*4]
        vldr    s10, [IN, #(2*8+1)*4]
        vldr    s12, [IN, #(2*8+2)*4]
        vldr    s14, [IN, #(2*8+3)*4]
        vneg.f  s17, s17
        vldr    s16, [IN, #(2*8+4)*4]
        vldr    s18, [IN, #(2*8+5)*4]
        vldr    s20, [IN, #(2*8+6)*4]
        vldr    s22, [IN, #(2*8+7)*4]
        vstr    d4,  [BUF, #(0*32+2)*4]
        vstr    d5,  [BUF, #(1*32+2)*4]
        vstr    d6,  [BUF, #(2*32+2)*4]
        vstr    d7,  [BUF, #(3*32+2)*4]
        vstr    d8,  [BUF, #(4*32+2)*4]
        vstr    d9,  [BUF, #(5*32+2)*4]
        vstr    d10, [BUF, #(6*32+2)*4]
        vstr    d11, [BUF, #(7*32+2)*4]
        add     IN, IN, #4*8*4
        add     BUF, BUF, #4*4
        subs    COUNT, COUNT, #(4 << 5) + 2
        bpl     1b
2:      @ Now deal with trailing < 4 samples
        adds    COUNT, COUNT, #3 << 5
        bmi     4f  @ sb_act was a multiple of 4
        bics    lr, COUNT, #0x1F
        bne     3f
        @ sb_act was n*4+1
        vldr    s8,  [IN, #(0*8+0)*4]
        vldr    s10, [IN, #(0*8+1)*4]
        vldr    s12, [IN, #(0*8+2)*4]
        vldr    s14, [IN, #(0*8+3)*4]
        vldr    s16, [IN, #(0*8+4)*4]
        vldr    s18, [IN, #(0*8+5)*4]
        vldr    s20, [IN, #(0*8+6)*4]
        vldr    s22, [IN, #(0*8+7)*4]
        vneg.f  s8, s8
        vldr    s9,  zero
        vldr    s11, zero
        vldr    s13, zero
        vldr    s15, zero
        vneg.f  s16, s16
        vldr    s17, zero
        vldr    s19, zero
        vldr    s21, zero
        vldr    s23, zero
        vstr    d4,  [BUF, #(0*32+0)*4]
        vstr    d5,  [BUF, #(1*32+0)*4]
        vstr    d6,  [BUF, #(2*32+0)*4]
        vstr    d7,  [BUF, #(3*32+0)*4]
        vstr    d8,  [BUF, #(4*32+0)*4]
        vstr    d9,  [BUF, #(5*32+0)*4]
        vstr    d10, [BUF, #(6*32+0)*4]
        vstr    d11, [BUF, #(7*32+0)*4]
        add     BUF, BUF, #2*4
        sub     COUNT, COUNT, #1
        b       4f
3:      @ sb_act was n*4+2 or n*4+3, so do the first 2
        vldr    s8,  [IN, #(0*8+0)*4]
        vldr    s10, [IN, #(0*8+1)*4]
        vldr    s12, [IN, #(0*8+2)*4]
        vldr    s14, [IN, #(0*8+3)*4]
        vldr    s16, [IN, #(0*8+4)*4]
        vldr    s18, [IN, #(0*8+5)*4]
        vldr    s20, [IN, #(0*8+6)*4]
        vldr    s22, [IN, #(0*8+7)*4]
        vneg.f  s8, s8
        vldr    s9,  [IN, #(1*8+0)*4]
        vldr    s11, [IN, #(1*8+1)*4]
        vldr    s13, [IN, #(1*8+2)*4]
        vldr    s15, [IN, #(1*8+3)*4]
        vneg.f  s16, s16
        vldr    s17, [IN, #(1*8+4)*4]
        vldr    s19, [IN, #(1*8+5)*4]
        vldr    s21, [IN, #(1*8+6)*4]
        vldr    s23, [IN, #(1*8+7)*4]
        vstr    d4,  [BUF, #(0*32+0)*4]
        vstr    d5,  [BUF, #(1*32+0)*4]
        vstr    d6,  [BUF, #(2*32+0)*4]
        vstr    d7,  [BUF, #(3*32+0)*4]
        vstr    d8,  [BUF, #(4*32+0)*4]
        vstr    d9,  [BUF, #(5*32+0)*4]
        vstr    d10, [BUF, #(6*32+0)*4]
        vstr    d11, [BUF, #(7*32+0)*4]
        add     BUF, BUF, #2*4
        sub     COUNT, COUNT, #(2 << 5) + 1
        bics    lr, COUNT, #0x1F
        bne     4f
        @ sb_act was n*4+3
        vldr    s8,  [IN, #(2*8+0)*4]
        vldr    s10, [IN, #(2*8+1)*4]
        vldr    s12, [IN, #(2*8+2)*4]
        vldr    s14, [IN, #(2*8+3)*4]
        vldr    s16, [IN, #(2*8+4)*4]
        vldr    s18, [IN, #(2*8+5)*4]
        vldr    s20, [IN, #(2*8+6)*4]
        vldr    s22, [IN, #(2*8+7)*4]
        vldr    s9,  zero
        vldr    s11, zero
        vldr    s13, zero
        vldr    s15, zero
        vldr    s17, zero
        vldr    s19, zero
        vldr    s21, zero
        vldr    s23, zero
        vstr    d4,  [BUF, #(0*32+0)*4]
        vstr    d5,  [BUF, #(1*32+0)*4]
        vstr    d6,  [BUF, #(2*32+0)*4]
        vstr    d7,  [BUF, #(3*32+0)*4]
        vstr    d8,  [BUF, #(4*32+0)*4]
        vstr    d9,  [BUF, #(5*32+0)*4]
        vstr    d10, [BUF, #(6*32+0)*4]
        vstr    d11, [BUF, #(7*32+0)*4]
        add     BUF, BUF, #2*4
        sub     COUNT, COUNT, #1
4:      @ Now fill the remainder with 0
        vldr    s8, zero
        vldr    s9, zero
        ands    COUNT, COUNT, #0x1F
        beq     6f
5:      vstr    d4, [BUF, #(0*32+0)*4]
        vstr    d4, [BUF, #(1*32+0)*4]
        vstr    d4, [BUF, #(2*32+0)*4]
        vstr    d4, [BUF, #(3*32+0)*4]
        vstr    d4, [BUF, #(4*32+0)*4]
        vstr    d4, [BUF, #(5*32+0)*4]
        vstr    d4, [BUF, #(6*32+0)*4]
        vstr    d4, [BUF, #(7*32+0)*4]
        add     BUF, BUF, #2*4
        subs    COUNT, COUNT, #1
        bne     5b
6:
        fmxr    FPSCR, OLDFPSCR
        ldr     WINDOW, [fp, #3*4]
        ldr     OUT, [fp, #4*4]
        sub     BUF, BUF, #32*4
NOVFP   ldr     SCALEINT, [fp, #6*4]
        mov     COUNT, #8
VFP     vpush   {SCALE}
VFP     sub     sp, sp, #3*4
NOVFP   sub     sp, sp, #4*4
7:
VFP     ldr     a1, [fp, #-7*4]     @ imdct
NOVFP   ldr     a1, [fp, #-8*4]
        ldmia   fp, {a2-a4}
VFP     stmia   sp, {WINDOW, OUT, BUF}
NOVFP   stmia   sp, {WINDOW, OUT, BUF, SCALEINT}
VFP     vldr    SCALE, [sp, #3*4]
        bl      X(ff_synth_filter_float_vfp)
        add     OUT, OUT, #32*4
        add     BUF, BUF, #32*4
        subs    COUNT, COUNT, #1
        bne     7b

A       sub     sp, fp, #(8+8)*4
T       sub     fp, fp, #(8+8)*4
T       mov     sp, fp
        vpop    {s16-s23}
VFP     pop     {a3-a4,v1-v3,v5,fp,pc}
NOVFP   pop     {a4,v1-v5,fp,pc}
endfunc

        .unreq  IN
        .unreq  SBACT
        .unreq  OLDFPSCR
        .unreq  IMDCT
        .unreq  WINDOW
        .unreq  OUT
        .unreq  BUF
        .unreq  SCALEINT
        .unreq  COUNT

        .unreq  SCALE

        .align 2
zero:   .word   0
