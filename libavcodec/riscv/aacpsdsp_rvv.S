/*
 * Copyright © 2022 Rémi Denis-Courmont.
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

#include "libavutil/riscv/asm.S"

func ff_ps_add_squares_rvv, zve32f
1:
        vsetvli     t0, a2, e32, m1, ta, ma
        vlseg2e32.v v24, (a1)
        sub         a2, a2, t0
        vle32.v     v16, (a0)
        sh3add      a1, t0, a1
        vfmacc.vv   v16, v24, v24
        vfmacc.vv   v16, v25, v25
        vse32.v     v16, (a0)
        sh2add      a0, t0, a0
        bnez        a2, 1b

        ret
endfunc

func ff_ps_mul_pair_single_rvv, zve32f
1:
        vsetvli     t0, a3, e32, m1, ta, ma
        vlseg2e32.v v24, (a1)
        sub         a3, a3, t0
        vle32.v     v16, (a2)
        sh3add      a1, t0, a1
        vfmul.vv    v24, v24, v16
        sh2add      a2, t0, a2
        vfmul.vv    v25, v25, v16
        vsseg2e32.v v24, (a0)
        sh3add      a0, t0, a0
        bnez        a3, 1b

        ret
endfunc

func ff_ps_hybrid_analysis_rvv, zve32f
        /* We need 26 FP registers, for 20 scratch ones. Spill fs0-fs5. */
        addi    sp, sp, -48
        .irp n, 0, 1, 2, 3, 4, 5
HWD     fsd     fs\n, (8 * \n)(sp)
NOHWD   fsw     fs\n, (4 * \n)(sp)
        .endr

        .macro input, j, fd0, fd1, fd2, fd3
        flw     \fd0, (4 * ((\j * 2) + 0))(a1)
        flw     fs4, (4 * (((12 - \j) * 2) + 0))(a1)
        flw     \fd1, (4 * ((\j * 2) + 1))(a1)
        fsub.s  \fd3, \fd0, fs4
        flw     fs5, (4 * (((12 - \j) * 2) + 1))(a1)
        fadd.s  \fd2, \fd1, fs5
        fadd.s  \fd0, \fd0, fs4
        fsub.s  \fd1, \fd1, fs5
        .endm

        //         re0, re1, im0, im1
        input   0, ft0, ft1, ft2, ft3
        input   1, ft4, ft5, ft6, ft7
        input   2, ft8, ft9, ft10, ft11
        input   3, fa0, fa1, fa2, fa3
        input   4, fa4, fa5, fa6, fa7
        input   5, fs0, fs1, fs2, fs3
        flw     fs4, (4 * ((6 * 2) + 0))(a1)
        flw     fs5, (4 * ((6 * 2) + 1))(a1)

        add        a2, a2, 6 * 2 * 4 // point to filter[i][6][0]
        li         t4, 8 * 2 * 4 // filter byte stride
        slli       a3, a3, 3 // output byte stride
1:
        .macro filter, vs0, vs1, fo0, fo1, fo2, fo3
        vfmacc.vf  v8, \fo0, \vs0
        vfmacc.vf  v9, \fo2, \vs0
        vfnmsac.vf v8, \fo1, \vs1
        vfmacc.vf  v9, \fo3, \vs1
        .endm

        vsetvli    t0, a4, e32, m1, ta, ma
        /*
         * The filter (a2) has 16 segments, of which 13 need to be extracted.
         * R-V V supports only up to 8 segments, so unrolling is unavoidable.
         */
        addi       t1, a2, -48
        vlse32.v   v22, (a2), t4
        addi       t2, a2, -44
        vlse32.v   v16, (t1), t4
        addi       t1, a2, -40
        vfmul.vf   v8, v22, fs4
        vlse32.v   v24, (t2), t4
        addi       t2, a2, -36
        vfmul.vf   v9, v22, fs5
        vlse32.v   v17, (t1), t4
        addi       t1, a2, -32
        vlse32.v   v25, (t2), t4
        addi       t2, a2, -28
        filter     v16, v24, ft0, ft1, ft2, ft3
        vlse32.v   v18, (t1), t4
        addi       t1, a2, -24
        vlse32.v   v26, (t2), t4
        addi       t2, a2, -20
        filter     v17, v25, ft4, ft5, ft6, ft7
        vlse32.v   v19, (t1), t4
        addi       t1, a2, -16
        vlse32.v   v27, (t2), t4
        addi       t2, a2, -12
        filter     v18, v26, ft8, ft9, ft10, ft11
        vlse32.v   v20, (t1), t4
        addi       t1, a2, -8
        vlse32.v   v28, (t2), t4
        addi       t2, a2, -4
        filter     v19, v27, fa0, fa1, fa2, fa3
        vlse32.v   v21, (t1), t4
        sub        a4, a4, t0
        vlse32.v   v29, (t2), t4
        slli       t1, t0, 3 + 1 + 2 // ctz(8 * 2 * 4)
        add        a2, a2, t1
        filter     v20, v28, fa4, fa5, fa6, fa7
        filter     v21, v29, fs0, fs1, fs2, fs3

        add        t2, a0, 4
        vsse32.v   v8, (a0), a3
        mul        t0, t0, a3
        vsse32.v   v9, (t2), a3
        add        a0, a0, t0
        bnez       a4, 1b

        .irp n, 5, 4, 3, 2, 1, 0
HWD     fld     fs\n, (8 * \n)(sp)
NOHWD   flw     fs\n, (4 * \n)(sp)
        .endr
        addi    sp, sp, 48
        ret
        .purgem input
        .purgem filter
endfunc

func ff_ps_hybrid_analysis_ileave_rvv, zve32x /* no needs for zve32f here */
        slli        t0, a2, 5 + 1 + 2 // ctz(32 * 2 * 4)
        sh2add      a1, a2, a1
        add         a0, a0, t0
        addi        a2, a2, -64
        li          t1, 38 * 64 * 4
        li          t6, 64 * 4 // (uint8_t *)L[x][j+1][i] - L[x][j][i]
        add         a4, a1, t1 // &L[1]
        beqz        a2, 3f
1:
        mv          t0, a0
        mv          t1, a1
        mv          t3, a3
        mv          t4, a4
        addi        a2, a2, 1
2:
        vsetvli     t5, t3, e32, m1, ta, ma
        vlse32.v    v16, (t1), t6
        sub         t3, t3, t5
        vlse32.v    v17, (t4), t6
        mul         t2, t5, t6
        vsseg2e32.v v16, (t0)
        sh3add      t0, t5, t0
        add         t1, t1, t2
        add         t4, t4, t2
        bnez        t3, 2b

        add         a0, a0, 32 * 2 * 4
        add         a1, a1, 4
        add         a4, a4, 4
        bnez        a2, 1b
3:
        ret
endfunc

func ff_ps_hybrid_synthesis_deint_rvv, zve32x
        slli        t1, a2, 5 + 1 + 2
        sh2add      a0, a2, a0
        add         a1, a1, t1
        addi        a2, a2, -64
        li          t1, 38 * 64 * 4
        li          t6, 64 * 4
        add         a4, a0, t1
        beqz        a2, 3f
1:
        mv          t0, a0
        mv          t1, a1
        mv          t3, a3
        mv          t4, a4
        addi        a2, a2, 1
2:
        vsetvli     t5, t3, e32, m1, ta, ma
        vlseg2e32.v v16, (t1)
        sub         t3, t3, t5
        vsse32.v    v16, (t0), t6
        mul         t2, t5, t6
        vsse32.v    v17, (t4), t6
        sh3add      t1, t5, t1
        add         t0, t0, t2
        add         t4, t4, t2
        bnez        t3, 2b

        add         a0, a0, 4
        add         a1, a1, 32 * 2 * 4
        add         a4, a4, 4
        bnez        a2, 1b
3:
        ret
endfunc

func ff_ps_stereo_interpolate_rvv, zve32f
        vsetvli      t0, zero, e32, m1, ta, ma
        vid.v        v24
        flw          ft0,   (a2)
        vadd.vi      v24, v24, 1   // v24[i] = i + 1
        flw          ft1,  4(a2)
        vfcvt.f.xu.v v24, v24
        flw          ft2,  8(a2)
        vfmv.v.f     v16, ft0
        flw          ft3, 12(a2)
        vfmv.v.f     v17, ft1
        flw          ft0,   (a3)
        vfmv.v.f     v18, ft2
        flw          ft1,  4(a3)
        vfmv.v.f     v19, ft3
        flw          ft2,  8(a3)
        vfmv.v.f     v20, ft0
        flw          ft3, 12(a3)
        vfmv.v.f     v21, ft1
        fcvt.s.wu    ft4, t0       // (float)(vlenb / sizeof (float))
        vfmv.v.f     v22, ft2
        fmul.s       ft0, ft0, ft4
        vfmv.v.f     v23, ft3
        fmul.s       ft1, ft1, ft4
        vfmacc.vv    v16, v24, v20 // h0 += (i + 1) * h0_step
        fmul.s       ft2, ft2, ft4
        vfmacc.vv    v17, v24, v21
        fmul.s       ft3, ft3, ft4
        vfmacc.vv    v18, v24, v22
        vfmacc.vv    v19, v24, v23
1:
        vsetvli   t0, a4, e32, m1, ta, ma
        vlseg2e32.v v8, (a0)     // v8:l_re, v9:l_im
        sub       a4, a4, t0
        vlseg2e32.v v10, (a1)    // v10:r_re, v11:r_im
        vfmul.vv  v12, v8, v16
        vfmul.vv  v13, v9, v16
        vfmul.vv  v14, v8, v17
        vfmul.vv  v15, v9, v17
        vfmacc.vv v12, v10, v18
        vfmacc.vv v13, v11, v18
        vfmacc.vv v14, v10, v19
        vfmacc.vv v15, v11, v19
        vsseg2e32.v v12, (a0)
        sh3add    a0, t0, a0
        vsseg2e32.v v14, (a1)
        sh3add    a1, t0, a1
        vfadd.vf  v16, v16, ft0 // h0 += (vlenb / sizeof (float)) * h0_step
        vfadd.vf  v17, v17, ft1
        vfadd.vf  v18, v18, ft2
        vfadd.vf  v19, v19, ft3
        bnez      a4, 1b

        ret
endfunc
