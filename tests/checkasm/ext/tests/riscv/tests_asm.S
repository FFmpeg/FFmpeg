/******************************************************************************
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2023, Nathan Egge
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#define PRIVATE_PREFIX selftest_

#include "src/checkasm_config.h"

#if ARCH_RISCV

#include "src/riscv/asm.S"

function copy_rvv, export=1, ext=zve32x
  vsetvli t0, a2, e8, m8, ta, ma
  vle8.v v0, (a1)
  add a1, a1, t0
  sub a2, a2, t0
  vse8.v v0, (a0)
  add a0, a0, t0
  bnez a2, copy_rvv
  ret
endfunc

.macro clobber_fn reg
function clobber_\reg, export=1
        not     \reg, \reg
        ret
endfunc
.endm

.irp reg, sp, gp, t0, t1, t2, s0, s1, a0, a1, a2, a3, a4, a5, a6, a7, \
          s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, t3, t4, t5, t6
clobber_fn \reg
.endr

function clobber_ra, export=1
        mv      t0, ra
        not     ra, ra
        jr      t0
endfunc

.macro clobber_f_fn reg
function clobber_\reg, export=1, ext=f
        fmv.x.w t0, \reg
        not     t0, t0
        fmv.w.x \reg, zero
        ret
endfunc
.endm

.irp reg, ft0, ft1, ft2, ft3, ft4, ft5, ft6, ft7, fs0, fs1, \
          fa0, fa1, fa2, fa3, fa4, fa5, fa6, fa7, fs2, fs3, \
          fs4, fs5, fs6, fs7, fs8, fs9, fs10, fs11, ft8, ft9, ft10, ft11
clobber_f_fn \reg
.endr

function sigill_riscv, export=1
  .word 0
endfunc

function corrupt_stack_riscv, export=1
        mv      t0, sp
        li      t1, (__riscv_xlen / 2)
1:
        addi    t1, t1, -1
        sw      zero, (t0)
        addi    t0, t0, 4
        bnez    t1, 1b

        ret
endfunc

#endif
