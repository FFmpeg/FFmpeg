/****************************************************************************
 * Copyright © 2022 Rémi Denis-Courmont.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#include "libavutil/riscv/asm.S"

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

func checkasm_set_function
        lpad    0
        la.tls.ie t0, .Lchecked_func
        add     t0, tp, t0
        sx      a0, (t0)
        ret
endfunc

func checkasm_get_wrapper, v
        lpad    0
        addi    sp, sp, -STACK_SPACE(2 * XSZ)
        sx      fp,     (sp)
        sx      ra, XSZ(sp)
        addi    fp, sp, STACK_SPACE(2 * XSZ)

        call    av_get_cpu_flags
        andi    t0, a0, 8 /* AV_CPU_FLAG_RVV_I32 */
#ifdef __riscv_float_abi_soft
        andi    t1, a0, 16 /* AV_CPU_FLAG_RVV_F32 (implies F and Zve32x) */
        lla     a0, checkasm_checked_call_i
        beqz    t0, 1f
        lla     a0, checkasm_checked_call_iv
        beqz    t1, 1f
#else
        lla     a0, checkasm_checked_call_if
        beqz    t0, 1f
#endif
        lla     a0, checkasm_checked_call_ifv
1:
        lx      ra, XSZ(sp)
        lx      fp,    (sp)
        addi    sp, sp, 16
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

func checkasm_checked_call_i
        /* <-- Entry point without the Vector extension --> */
        lpad    0
        /* Save RA, unallocatable and callee-saved registers */
        la.tls.ie t0, .Lsaved_xregs
        add     t0, tp, t0
        sx      ra,        (t0)
        sx      sp, 1 * XSZ(t0)
        sx      gp, 2 * XSZ(t0)
        sx      tp, 3 * XSZ(t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sx      s\n, (4 + \n) * XSZ(t0)
        .endr

        /* Clobber the stack space right below SP */
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

        /* Call the tested function */
        la.tls.ie t0, .Lchecked_func
        add     t0, tp, t0
        lx      t3, (t0)
        sx      zero, (t0)
        jalr    t3

        /* Check special register values */
        la.tls.ie t0, .Lsaved_xregs
        add     t0, tp, t0
        lx      t2, 1 * XSZ(t0) // SP
        lx      t3, 2 * XSZ(t0) // GP
        lx      t4, 3 * XSZ(t0) // TP
        li      t1, 'S'
        bne     t2, sp, .Lfail_xp
        li      t1, 'G'
        bne     t3, gp, .Lfail_xp
        li      t1, 'T'
        bne     t4, tp, .Lfail_xp

        /* Check value of saved registers */
        li      t0, REG_MAGIC
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        li      t1, \n
        bne     t0, s\n, .Lfail_s
        .endr

4:
        /* Restore RA and saved registers */
        la.tls.ie t0, .Lsaved_xregs
        add     t0, tp, t0
        lx      ra, (t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        lx      s\n, (4 + \n) * XSZ(t0)
        .endr
        ret

.Lfail_xp:
        # checkasm_fail_func() needs valid SP, GP and TP. Restore them.
        lx      sp, 1 * XSZ(t0)
        lx      gp, 2 * XSZ(t0)
        lx      tp, 3 * XSZ(t0)
        lla     a0, .Lfail_rsvd_reg
        mv      a1, t1
        call    checkasm_fail_func
        j       4b

.Lfail_s:
        lla     a0, .Lfail_s_reg
        mv      a1, t1
        call    checkasm_fail_func
        j       4b
endfunc

#ifndef __riscv_float_abi_soft
func checkasm_checked_call_if, f
        lpad    0
        # Save callee-saved floating point registers and RA
        la.tls.ie t0, .Lsaved_fregs
        add     t0, t0, tp
        lla     t1, .Lbad_float
        sd      ra, 12 * FSZ(t0)
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

1:      # Restore callee-saved floating point registers and RA
        la.tls.ie t0, .Lsaved_fregs
        add     t0, t0, tp
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        flf     fs\n, \n * FSZ(t0)
        .endr
        ld      ra, 12 * FSZ(t0)
        ret

.Lfail_fs:
        lla     a0, .Lfail_fs_reg
        mv      a1, t1
        call    checkasm_fail_func
        j       1b
endfunc
#else
func checkasm_checked_call_if, f
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

func checkasm_checked_call_iv, zve32x
        lpad    0
        jal     t0, .Lclobber_v
        j       checkasm_checked_call_i
endfunc
#endif

func checkasm_checked_call_ifv, zve32x
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
