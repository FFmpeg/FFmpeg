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

#if (__riscv_xlen == 64)

const fail_s_reg
        .asciz  "callee-saved integer register clobbered"
endconst

const fail_fs_reg
        .asciz  "callee-saved floating-point register clobbered"
endconst

const fail_rsvd_reg
        .asciz  "unallocatable register clobbered"
endconst

        .section .tbss, "waT"
        .align  3
        .hidden checked_func
        .hidden saved_regs

checked_func:
        .quad   0

saved_regs:
        /* Space to spill RA, SP, GP, TP, S0-S11 and FS0-FS11 */
        .rept   4 + 12 + 12
        .quad   0
        .endr

func checkasm_set_function
        lpad    0
        la.tls.ie t0, checked_func
        add     t0, tp, t0
        sd      a0, (t0)
        ret
endfunc

func checkasm_get_wrapper, v
        lpad    0
        addi    sp, sp, -16
        sd      fp,  (sp)
        sd      ra, 8(sp)
        addi    fp, sp, 16

        call    av_get_cpu_flags
        andi    t0, a0, 8 /* AV_CPU_FLAG_RVV_I32 */

        lla     a0, 3f
        beqz    t0, 1f
        lla     a0, 2f
1:
        ld      ra, 8(sp)
        ld      fp,  (sp)
        addi    sp, sp, 16
        ret

        .option norvc
        .align  2
2:      /* <-- Entry point with the Vector extension --> */
        lpad    0
        /* Clobber the vectors */
        vsetvli t0, zero, e32, m8, ta, ma
        li      t0, 0xdeadbeef
        vmv.v.x v0, t0
        vmv.v.x v8, t0
        vmv.v.x v16, t0
        vmv.v.x v24, t0

        /* Clobber the vector configuration */
        li      t0, 0        /* Vector length: zero */
        li      t2, -4       /* Vector type:   illegal */
        vsetvl  zero, t0, t2
        csrwi   vxrm, 3      /* Rounding mode: round-to-odd */
        csrwi   vxsat, 1     /* Saturation:    encountered */

        .align  2
3:      /* <-- Entry point without the Vector extension --> */
        lpad    0
        /* Save RA, unallocatable and callee-saved registers */
        la.tls.ie t0, saved_regs
        add     t0, tp, t0
        sd      ra,   (t0)
        sd      sp,  8(t0)
        sd      gp, 16(t0)
        sd      tp, 24(t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        sd      s\n, (32 + (16 * \n))(t0)
        fsd     fs\n, (40 + (16 * \n))(t0)
        .endr

        /* Clobber the stack space right below SP */
        li      t0, 0xdeadbeef1badf00d
        .rept   16
        addi    sp, sp, -16
        sd      t0,  (sp)
        sd      t0, 8(sp)
        .endr
        addi    sp, sp, 256

        /* Clobber the saved and temporary registers */
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        .if (\n > 1 && \n < 7)
        mv      t\n, t0
        .endif
        fmv.d.x ft\n, t0
        mv      s\n, t0
        fmv.d.x fs\n, t0
        .endr

        /* Call the tested function */
        la.tls.ie t0, checked_func
        add     t0, tp, t0
        ld      t3, (t0)
        sd      zero, (t0)
        jalr    t3

        /* Check special register values */
        la.tls.ie t0, saved_regs
        add     t0, tp, t0
        ld      t1,  8(t0)
        bne     t1, sp, 5f
        ld      t1, 16(t0)
        bne     t1, gp, 5f
        ld      t1, 24(t0) // If TP was corrupted, we probably will have...
        bne     t1, tp, 5f // ...already crashed before we even get here.

        /* Check value of saved registers */
        li      t0, 0xdeadbeef1badf00d
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        bne     t0, s\n, 6f
#ifdef __riscv_float_abi_double
        /* TODO: check float ABI single too */
        fmv.x.d t1, fs\n
        bne     t0, t1, 7f
#endif
        .endr

4:
        /* Restore RA and saved registers */
        la.tls.ie t0, saved_regs
        add     t0, tp, t0
        ld      ra,   (t0)
        .irp    n, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
        ld      s\n, (32 + (16 * \n))(t0)
        fld     fs\n, (40 + (16 * \n))(t0)
        .endr
        ret

5:
        lla     a0, fail_rsvd_reg
        call    checkasm_fail_func
        tail    abort /* The test harness would probably crash anyway */

6:
        lla     a0, fail_s_reg
        call    checkasm_fail_func
        j       4b

7:
        lla     a0, fail_fs_reg
        call    checkasm_fail_func
        j       4b
endfunc
#endif
