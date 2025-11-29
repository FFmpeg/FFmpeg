/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright © 2025 Rémi Denis-Courmont.
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

const offsets_8, 1
        .short 0, 64, 256, 320
endconst

func ff_h264_luma_dc_dequant_idct_8_rvv, zve32x
        lpad   0
        csrwi  vxrm, 0
        vsetivli    zero, 4, e16, mf2, ta, ma
        vlseg4e16.v v8, (a1)
        vwadd.vv    v16, v8, v9     # z0
        addi    t1, sp, 4 * 4 * -3
        vwadd.vv    v19, v10, v11   # z3
        addi    t2, sp, 4 * 4 * -2
        vwsub.vv    v17, v8, v9     # z1
        addi    t3, sp, 4 * 4 * -1
        vwsub.vv    v18, v10, v11   # z2
        vsetvli zero, zero, e32, m1, ta, ma
        vadd.vv v8, v16, v19
        addi    sp, sp, 4 * 4 * -4
        vsub.vv v9, v16, v19
        vsub.vv v10, v17, v18
        vadd.vv v11, v17, v18
        vsseg4e32.v v8, (sp)
        vle32.v v8, (sp)
        vle32.v v9, (t1)
        vle32.v v10, (t2)
        vle32.v v11, (t3)
        vadd.vv v16, v8, v10    # z0
        addi    sp, sp, 4 * 4 * 4
        vadd.vv v19, v9, v11    # z3
        lla     t0, offsets_8
        vsub.vv v17, v8, v10    # z1
        vsub.vv v18, v9, v11    # z2
        vadd.vv v8, v16, v19
        vadd.vv v9, v17, v18
        vsub.vv v10, v17, v18
        vsub.vv v11, v16, v19
        vle16.v v24, (t0)
        vmul.vx v8, v8, a2
        vmul.vx v9, v9, a2
        vmul.vx v10, v10, a2
        vmul.vx v11, v11, a2
        vsetvli zero, zero, e16, mf2, ta, ma
        vnclip.wi   v16, v8, 8
        addi    t1, a0, 2 * 16 * 1
        vnclip.wi   v17, v9, 8
        addi    t2, a0, 2 * 16 * 4
        vnclip.wi   v18, v10, 8
        addi    t3, a0, 2 * 16 * 5
        vnclip.wi   v19, v11, 8
        vsuxei16.v  v16, (a0), v24
        vsuxei16.v  v17, (t1), v24
        vsuxei16.v  v18, (t2), v24
        vsuxei16.v  v19, (t3), v24
        ret
endfunc

const offsets_9, 1
        .short 0, 128, 512, 640
endconst

func ff_h264_luma_dc_dequant_idct_9_rvv, zve32x
        lpad   0
        csrwi  vxrm, 0
        vsetivli    zero, 4, e32, m1, ta, ma
        vlseg4e32.v v8, (a1)
        vadd.vv v16, v8, v9     # z0
        addi    t1, sp, 4 * 4 * -3
        vadd.vv v19, v10, v11   # z3
        addi    t2, sp, 4 * 4 * -2
        vsub.vv v17, v8, v9     # z1
        addi    t3, sp, 4 * 4 * -1
        vsub.vv v18, v10, v11   # z2
        vadd.vv v8, v16, v19
        addi    sp, sp, 4 * 4 * -4
        vsub.vv v9, v16, v19
        vsub.vv v10, v17, v18
        vadd.vv v11, v17, v18
        vsseg4e32.v v8, (sp)
        vle32.v v8, (sp)
        vle32.v v9, (t1)
        vle32.v v10, (t2)
        vle32.v v11, (t3)
        vadd.vv v16, v8, v10    # z0
        addi    sp, sp, 4 * 4 * 4
        vadd.vv v19, v9, v11    # z3
        lla     t0, offsets_9
        vsub.vv v17, v8, v10    # z1
        vsub.vv v18, v9, v11    # z2
        vadd.vv v8, v16, v19
        vadd.vv v9, v17, v18
        vsub.vv v10, v17, v18
        vsub.vv v11, v16, v19
        vle16.v v24, (t0)
        vmul.vx v8, v8, a2
        vmul.vx v9, v9, a2
        vmul.vx v10, v10, a2
        vmul.vx v11, v11, a2
        vssra.vi    v16, v8, 8
        addi    t1, a0, 4 * 16 * 1
        vssra.vi    v17, v9, 8
        addi    t2, a0, 4 * 16 * 4
        vssra.vi    v18, v10, 8
        addi    t3, a0, 4 * 16 * 5
        vssra.vi    v19, v11, 8
        vsuxei16.v  v16, (a0), v24
        vsuxei16.v  v17, (t1), v24
        vsuxei16.v  v18, (t2), v24
        vsuxei16.v  v19, (t3), v24
        ret
endfunc
