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

const ff_h264_scan8
        .byte   014, 015, 024, 025, 016, 017, 026, 027
        .byte   034, 035, 044, 045, 036, 037, 046, 047
endconst

#if (__riscv_xlen == 64)
.irp    depth, 8
func ff_h264_idct_add16_\depth\()_rvv, zve32x
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
        call    ff_h264_idct_add_\depth\()_c
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
        call    ff_h264_idct_add_\depth\()_c
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
.endr
#endif
