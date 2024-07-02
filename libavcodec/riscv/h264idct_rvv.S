/*
 * Copyright © 2024 Rémi Denis-Courmont.
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
 */

#include "libavutil/riscv/asm.S"

        .macro  sx rd, addr
#if (__riscv_xlen == 32)
        sw      \rd, \addr
#elif (__riscv_xlen == 64)
        sd      \rd, \addr
#else
        sq      \rd, \addr
#endif
        .endm

        .variant_cc ff_h264_idct4_rvv
func ff_h264_idct4_rvv, zve32x
        vsra.vi v5, v1, 1
        vsra.vi v7, v3, 1
        vadd.vv v8, v0, v2   # z0
        vsub.vv v9, v0, v2   # z1
        vsub.vv v10, v5, v3  # z2
        vadd.vv v11, v1, v7  # z3
        vadd.vv v1, v9, v10
        vsub.vv v2, v9, v10
        vadd.vv v0, v8, v11
        vsub.vv v3, v8, v11
        jr      t0
endfunc

func ff_h264_idct_add_8_rvv, zve32x
        csrwi       vxrm, 0
.Lidct_add4_8_rvv:
        vsetivli    zero, 4, e16, mf2, ta, ma
        addi        t1, a1, 1 * 4 * 2
        vle16.v     v0, (a1)
        addi        t2, a1, 2 * 4 * 2
        vle16.v     v1, (t1)
        addi        t3, a1, 3 * 4 * 2
        vle16.v     v2, (t2)
        vle16.v     v3, (t3)
        jal         t0, ff_h264_idct4_rvv
        vse16.v     v0, (a1)
        vse16.v     v1, (t1)
        vse16.v     v2, (t2)
        vse16.v     v3, (t3)
        vlseg4e16.v v0, (a1)
        .rept   256 / __riscv_xlen
        sx      zero, ((__riscv_xlen / 8) * \+)(a1)
        .endr
        jal         t0, ff_h264_idct4_rvv
        add         t1, a0, a2
        vle8.v      v4, (a0)
        add         t2, t1, a2
        vle8.v      v5, (t1)
        add         t3, t2, a2
        vle8.v      v6, (t2)
        vle8.v      v7, (t3)
        .irp    n,0,1,2,3
        vssra.vi    v\n, v\n, 6
        .endr
        vsetvli     zero, zero, e8, mf4, ta, ma
        vwaddu.wv   v0, v0, v4
        vwaddu.wv   v1, v1, v5
        vwaddu.wv   v2, v2, v6
        vwaddu.wv   v3, v3, v7
        vsetvli     zero, zero, e16, mf2, ta, ma
        .irp    n,0,1,2,3
        vmax.vx     v\n, v\n, zero
        .endr
        vsetvli     zero, zero, e8, mf4, ta, ma
        vnclipu.wi  v4, v0, 0
        vnclipu.wi  v5, v1, 0
        vnclipu.wi  v6, v2, 0
        vnclipu.wi  v7, v3, 0
        vse8.v      v4, (a0)
        vse8.v      v5, (t1)
        vse8.v      v6, (t2)
        vse8.v      v7, (t3)
        ret
endfunc

const ff_h264_scan8
        .byte   014, 015, 024, 025, 016, 017, 026, 027
        .byte   034, 035, 044, 045, 036, 037, 046, 047
endconst

#if (__riscv_xlen == 64)
.irp    depth, 8
func ff_h264_idct_add16_\depth\()_rvv, zve32x
        csrwi   vxrm, 0
        addi    sp, sp, -80
        lla     t0, ff_h264_scan8
        sd      s0,   (sp)
        li      t1, 32 << (\depth > 8)
        mv      s0, sp
        sd      ra,  8(sp)
        sd      s1, 16(sp)
        sd      s2, 24(sp)
        sd      s3, 32(sp)
        sd      s4, 40(sp)
        sd      s5, 48(sp)
        sd      s6, 56(sp)
        sd      s7, 64(sp)
        vsetivli  zero, 16, e8, m1, ta, ma
        vle8.v    v8, (t0)
        vlse16.v  v16, (a2), t1
        vluxei8.v v12, (a4), v8
.if \depth == 8
        vsetvli   zero, zero, e16, m2, ta, ma
.else
        vsetvli   zero, zero, e32, m4, ta, ma
.endif
        vmsne.vi  v1, v16, 0
        vsetvli   zero, zero, e8, m1, ta, ma
        vmseq.vi  v2, v12, 1
        vmsne.vi  v0, v12, 0
        vmand.mm  v1, v1, v2
        vsetvli   zero, zero, e16, m2, ta, ma
        vmv.x.s   s2, v0
        vmv.x.s   s3, v1
        li      s1, 16
        mv      s4, a0
        mv      s5, a1
        mv      s6, a2
        mv      s7, a3
1:
        andi    t0, s2, 1
        addi    s1, s1, -1
        srli    s2, s2, 1
        beqz    t0, 3f     # if (nnz)
        lw      t2, (s5)   # block_offset[i]
        andi    t1, s3, 1
        mv      a1, s6
        mv      a2, s7
        add     a0, s4, t2
        beqz    t1, 2f    # if (nnz == 1 && block[i * 16])
        call    ff_h264_idct_dc_add_\depth\()_c
        j       3f
2:
        call    .Lidct_add4_\depth\()_rvv
3:
        srli    s3, s3, 1
        addi    s5, s5, 4
        addi    s6, s6, 16 * 2 << (\depth > 8)
        bnez    s1, 1b

        ld      s7, 64(sp)
        ld      s6, 56(sp)
        ld      s5, 48(sp)
        ld      s4, 40(sp)
        ld      s3, 32(sp)
        ld      s2, 24(sp)
        ld      s1, 16(sp)
        ld      ra,  8(sp)
        ld      s0,  0(sp)
        addi    sp, sp, 80
        ret
endfunc

func ff_h264_idct_add16intra_\depth\()_rvv, zve32x
        csrwi   vxrm, 0
        addi    sp, sp, -80
        lla     t0, ff_h264_scan8
        sd      s0,   (sp)
        li      t1, 32 << (\depth > 8)
        mv      s0, sp
        sd      ra,  8(sp)
        sd      s1, 16(sp)
        sd      s2, 24(sp)
        sd      s3, 32(sp)
        sd      s4, 40(sp)
        sd      s5, 48(sp)
        sd      s6, 56(sp)
        sd      s7, 64(sp)
        vsetivli  zero, 16, e8, m1, ta, ma
        vle8.v    v8, (t0)
        vlse16.v  v16, (a2), t1
        vluxei8.v v12, (a4), v8
.if \depth == 8
        vsetvli   zero, zero, e16, m2, ta, ma
.else
        vsetvli   zero, zero, e32, m4, ta, ma
.endif
        vmsne.vi  v1, v16, 0
        vsetvli   zero, zero, e8, m1, ta, ma
        vmsne.vi  v0, v12, 0
        vsetvli   zero, zero, e16, m2, ta, ma
        vmv.x.s   s2, v0
        vmv.x.s   s3, v1
        li      s1, 16
        mv      s4, a0
        mv      s5, a1
        mv      s6, a2
        mv      s7, a3
1:
        andi    t0, s2, 1
        addi    s1, s1, -1
        srli    s2, s2, 1
        lw      t2, (s5)   # block_offset[i]
        andi    t1, s3, 1
        mv      a1, s6
        mv      a2, s7
        add     a0, s4, t2
        beqz    t0, 2f     # if (nnzc[scan8[i]])
        call    .Lidct_add4_\depth\()_rvv
        j       3f
2:
        beqz    t1, 3f    # if (block[i * 16])
        call    ff_h264_idct_dc_add_\depth\()_c
3:
        srli    s3, s3, 1
        addi    s5, s5, 4
        addi    s6, s6, 16 * 2 << (\depth > 8)
        bnez    s1, 1b

        ld      s7, 64(sp)
        ld      s6, 56(sp)
        ld      s5, 48(sp)
        ld      s4, 40(sp)
        ld      s3, 32(sp)
        ld      s2, 24(sp)
        ld      s1, 16(sp)
        ld      ra,  8(sp)
        ld      s0,  0(sp)
        addi    sp, sp, 80
        ret
endfunc

func ff_h264_idct8_add4_\depth\()_rvv, zve32x
        addi    sp, sp, -80
        lla     t0, ff_h264_scan8
        sd      s0,   (sp)
        li      t1, 4 * 32 << (\depth > 8)
        mv      s0, sp
        li      t2, 4
        sd      ra,  8(sp)
        sd      s1, 16(sp)
        sd      s2, 24(sp)
        sd      s3, 32(sp)
        sd      s4, 40(sp)
        sd      s5, 48(sp)
        sd      s6, 56(sp)
        sd      s7, 64(sp)
        vsetivli  zero, 4, e8, mf4, ta, ma
        vlse8.v   v8, (t0), t2
        vlse16.v  v16, (a2), t1
        vluxei8.v v12, (a4), v8
.if \depth == 8
        vsetvli   zero, zero, e16, mf2, ta, ma
.else
        vsetvli   zero, zero, e32, m1, ta, ma
.endif
        vmsne.vi  v1, v16, 0
        vsetvli   zero, zero, e8, mf4, ta, ma
        vmseq.vi  v2, v12, 1
        vmsne.vi  v0, v12, 0
        vmand.mm  v1, v1, v2
        vmv.x.s   s2, v0
        vmv.x.s   s3, v1
        li      s1, 4
        mv      s4, a0
        mv      s5, a1
        mv      s6, a2
        mv      s7, a3
1:
        andi    t0, s2, 1
        addi    s1, s1, -1
        srli    s2, s2, 1
        beqz    t0, 3f     # if (nnz)
        lw      t2, (s5)   # block_offset[i]
        andi    t1, s3, 1
        mv      a1, s6
        mv      a2, s7
        add     a0, s4, t2
        beqz    t1, 2f    # if (nnz == 1 && block[i * 16])
        call    ff_h264_idct8_dc_add_\depth\()_c
        j       3f
2:
        call    ff_h264_idct8_add_\depth\()_c
3:
        srli    s3, s3, 1
        addi    s5, s5, 4 * 4
        addi    s6, s6, 4 * 16 * 2 << (\depth > 8)
        bnez    s1, 1b

        ld      s7, 64(sp)
        ld      s6, 56(sp)
        ld      s5, 48(sp)
        ld      s4, 40(sp)
        ld      s3, 32(sp)
        ld      s2, 24(sp)
        ld      s1, 16(sp)
        ld      ra,  8(sp)
        ld      s0,  0(sp)
        addi    sp, sp, 80
        ret
endfunc
.endr
#endif
