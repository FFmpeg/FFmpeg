/****************************************************************************
 * Copyright © 2022-2025 Rémi Denis-Courmont.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include "checkasm_config.h"

#if ARCH_RISCV

#include "riscv/asm.S"

#if (__riscv_xlen == 32)
.macro  lx      rd, addr
        lw      \rd, \addr
.endm

.macro  sx      rs, addr
        sw      \rs, \addr
.endm
#define REG_MAGIC 0xdeadbeef
#elif (__riscv_xlen == 64)
.macro  lx      rd, addr
        ld      \rd, \addr
.endm

.macro  sx      rs, addr
        sd      \rs, \addr
.endm
#define REG_MAGIC 0xdeadbeef0badf00d
#else
.macro  lx      rd, addr
        lq      \rd, \addr
.endm

.macro  sx      rs, addr
        sq      \rs, \addr
.endm
#define REG_MAGIC 0xdeadbeef0badf00daaaabbbbccccdddd
#endif
#define XSZ             (__riscv_xlen / 8)
#define STACK_ALIGN     16
#define STACK_SPACE(sz) (((sz) + (STACK_ALIGN - 1)) & -STACK_ALIGN)

#if defined(__riscv_float_abi_soft)
.macro  flf     rd, addr
.endm
.macro  fsf     rs, addr
.endm
#define FSZ 0
#elif defined(__riscv_float_abi_single)
.macro  flf     rd, addr
        flw     \rd, \addr
.endm
.macro  fsf     rs, addr
        fsw     \rs, \addr
.endm
#define FSZ 4
#elif defined(__riscv_float_abi_double)
.macro  flf     rd, addr
        fld     \rd, \addr
.endm
.macro  fsf     rs, addr
        fsd     \rs, \addr
.endm
#define FSZ 8
#elif defined(__riscv_float_abi_quad)
.macro  flf     rd, addr
        flq     \rd, \addr
.endm
.macro  fsf     rs, addr
        fsq     \rs, \addr
.endm
#define FSZ 16
#else
#error "Unknown float ABI"
#endif

function clobber_args, export=1, visible=1
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7
        li      a\n, -\n
#ifndef __riscv_float_abi_soft
        fcvt.s.w    fa\n, a\n
#endif
        .endr
        ret
endfunc

        .pushsection .tbss, "waT"
        .align  4
.Lchecked_func:
        .fill   1, XSZ, 0
        .align  4
.Lsaved_xregs:
        .fill   4 + 12, XSZ, 0 // RA, SP, GP, TP, S0-S11
        .align  4
.Lsaved_fregs:
        .fill   12, FSZ, 0 // FS0-FS11
        .fill   1, XSZ, 0 // RA
        .popsection

function set_function, export=1, visible=1
        lpad    0
        la.tls.ie t0, .Lchecked_func
        add     t0, tp, t0
        sx      a0, (t0)
        ret
endfunc

        .pushsection ".rodata", "a"
.Lfail_s_reg:
        .asciz  "callee-saved integer register S%d clobbered"
.Lfail_fs_reg:
        .asciz  "callee-saved floating-point register FS%d clobbered"
.Lfail_rsvd_reg:
        .asciz  "unallocatable register %cP clobbered"
#if defined(__riscv_float_abi_soft) || defined(__riscv_float_abi_single)
        .align  2
.Lbad_float:
        .single 123456789
#elif defined(__riscv_float_abi_double)
        .align  3
.Lbad_float:
        .double 123456789
#elif defined(__riscv_float_abi_quad)
        .align  4
.Lbad_float:
        .ldouble 123456789
#endif
        .popsection

function checked_call_i, export=1
        lpad    0
        # Save RA, unallocatable and callee-saved registers
        la.tls.ie t0, .Lsaved_xregs
        add     t0, t0, tp
        sx      ra,        (t0)
        sx      sp, 1 * XSZ(t0)
        sx      gp, 2 * XSZ(t0)
        sx      tp, 3 * XSZ(t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sx      s\n, (4 + \n) * XSZ(t0)
        .endr

        # Clobber the stack space right below SP
        li      t1, REG_MAGIC
        li      t0, 16
1:
        addi    sp, sp, -XSZ
        addi    t0, t0, -1
        sx      t1, (sp)
        bnez    t0, 1b

        addi    sp, sp, 16 * XSZ
        # Clobber temporary registers (except T2, FE-CFI label)
        .irp    n, 0, 1, 3, 4, 5, 6
        mv      t\n, t1
        .endr
        # Clobber the saved registers
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        mv      s\n, t1
        .endr

        # Call the tested function (NOT using T1 to enforce FE-CFI labelling)
        la.tls.ie t0, .Lchecked_func
        add     t0, t0, tp
        lx      t3, (t0)
        sx      zero, (t0)
        jalr    t3

        # Check special register values
        la.tls.ie t0, .Lsaved_xregs
        add     t0, t0, tp
        lx      t2, 1 * XSZ(t0) // SP
        lx      t3, 2 * XSZ(t0) // GP
        lx      t4, 3 * XSZ(t0) // TP
        li      t1, 'S'
        bne     t2, sp, .Lfail_xp
        li      t1, 'G'
        bne     t3, gp, .Lfail_xp
        li      t1, 'T'
        # NOTE: If TP was corrupted, this will probably crash at `lx` above.
        bne     t4, tp, .Lfail_xp

        # Check saved register values
        li      t0, REG_MAGIC
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        li      t1, \n
        bne     t0, s\n, .Lfail_s
        .endr

        # Restore RA and callee-saved registers
        la.tls.ie t0, .Lsaved_xregs
        add     t0, t0, tp
        lx      ra, (t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        lx      s\n, (XSZ * (4 + \n))(t0)
        .endr
        ret

.Lfail_xp:
        # checkasm_fail_abort() needs valid SP, GP and TP. Restore them.
        lx      sp, 1 * XSZ(t0)
        lx      gp, 2 * XSZ(t0)
        lx      tp, 3 * XSZ(t0)
        lla     a0, .Lfail_rsvd_reg
        mv      a1, t1
        tail    checkasm_fail_abort

.Lfail_s:
        lla     a0, .Lfail_s_reg
        mv      a1, t1
        tail    checkasm_fail_abort
endfunc

#ifndef __riscv_float_abi_soft
function checked_call_if, export=1
        lpad    0
        # Save callee-saved floating point registers and RA
        la.tls.ie t0, .Lsaved_fregs
        add     t0, t0, tp
        lla     t1, .Lbad_float
        sx      ra, 12 * FSZ(t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        fsf     fs\n, \n * FSZ(t0)
        .endr
        # Clobber the saved and temporary floating point registers
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        flf     ft\n, (t1)
        flf     fs\n, (t1)
        .endr

        jal     checkasm_checked_call_i

        # Check value of saved registers
        lla     t1, .Lbad_float
        flf     ft0, (t1)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        li      t1, \n
#if defined(__riscv_float_abi_single)
        feq.s   t0, ft0, fs\n
#elif defined(__riscv_float_abi_double)
        feq.d   t0, ft0, fs\n
#else
        feq.q   t0, ft0, fs\n
#endif
        beqz    t0, .Lfail_fs
        .endr

        # Restore callee-saved floating point registers and RA
        la.tls.ie t0, .Lsaved_fregs
        add     t0, t0, tp
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        flf     fs\n, \n * FSZ(t0)
        .endr
        lx      ra, 12 * FSZ(t0)
        ret

.Lfail_fs:
        lla     a0, .Lfail_fs_reg
        mv      a1, t1
        tail    checkasm_fail_abort
endfunc
#else
function checked_call_if, export=1, ext=f
        lpad    0
        lla     t1, .Lbad_float
        # Clobber all floating point registers (soft float ABI).
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7
        flw     fa\n, (t1)
        .endr
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        flw     ft\n, (t1)
        flw     fs\n, (t1)
        .endr
        j       checkasm_checked_call_i
endfunc

function checked_call_iv, export=1, ext=zve32x
        lpad    0
        jal     t0, .Lclobber_v
        j       checkasm_checked_call_i
endfunc
#endif

function checked_call_ifv, export=1, ext=zve32x
        lpad    0
        jal     t0, .Lclobber_v
        j       checkasm_checked_call_if

.Lclobber_v:
        # Clobber the vector registers
        vsetvli t1, zero, e32, m8, ta, ma
        li      t1, -0xdeadbeef
        vmv.v.x v0, t1
        vmv.v.x v8, t1
        vmv.v.x v16, t1
        vmv.v.x v24, t1
        # Clobber the vector configuration
        li      t1, 0        /* Vector length: zero */
        li      t3, -4       /* Vector type:   illegal */
        vsetvl  zero, t1, t3
        csrwi   vxrm, 3      /* Rounding mode: round-to-odd */
        csrwi   vxsat, 1     /* Saturation:    encountered */
        jr      t0
endfunc

function get_vlen, export=1, ext="zve32x,zicsr"
        csrr    a0, vlenb
        slli    a0, a0, 0x3
        ret
endfunc

#endif
