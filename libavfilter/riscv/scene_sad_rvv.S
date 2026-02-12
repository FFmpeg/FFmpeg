/*
 * Copyright © 2026 Rodrigo Camacho.
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

/*
 * void ff_scene_sad8_rvv(const uint8_t *src1, ptrdiff_t stride1,
 *                        const uint8_t *src2, ptrdiff_t stride2,
 *                        ptrdiff_t width, ptrdiff_t height,
 *                        uint64_t *sum)
*/
#if __riscv_xlen >= 64
func ff_scene_sad8_rvv, zve64x
        lpad        0

        bnez       a5, 3f
        sd         zero, 0(a6)
        ret
3:
        vsetvli    t0, zero, e64, m8, ta, ma
        vmv.v.x    v24, zero

        sub        a1, a1, a4
        sub        a3, a3, a4
1:
        mv         t0, a4
        addi       a5, a5, -1

        vsetvli    zero, zero, e32, m4, ta, ma
        vmv.v.x    v16, zero
2:
        vsetvli    t1, t0, e8, m1, ta, ma
        vle8.v     v0, (a0)
        vle8.v     v1, (a2)

        add        a0, a0, t1
        vmaxu.vv   v2, v0, v1
        vminu.vv   v3, v0, v1
        sub        t0, t0, t1
        vwsubu.vv  v4, v2, v3
        add        a2, a2, t1

        vsetvli    zero, t1, e16, m2, tu, ma
        vwaddu.wv  v16, v16, v4

        bnez       t0, 2b

        add        a0, a0, a1
        vsetvli    t0, zero, e32, m4, ta, ma
        add        a2, a2, a3
        vwaddu.wv  v24, v24, v16

        bnez       a5, 1b

        vsetvli    zero, zero, e64, m8, ta, ma
        vmv.s.x    v0, zero
        vredsum.vs v0, v24, v0
        vmv.x.s    a7, v0

        sd         a7, 0(a6)
        ret
endfunc

/*
 * void ff_scene_sad16_rvv(const uint8_t *src1, ptrdiff_t stride1,
 *                        const uint8_t *src2, ptrdiff_t stride2,
 *                        ptrdiff_t width, ptrdiff_t height,
 *                        uint64_t *sum)
*/
func ff_scene_sad16_rvv, zve64x
        lpad       0

        bnez       a5, 3f
        sd         zero, 0(a6)
        ret
3:
        slli       t3, a4, 1

        vsetvli    t0, zero, e64, m8, ta, ma
        vmv.v.x    v24, zero

        sub        a1, a1, t3
        sub        a3, a3, t3
1:
        mv         t0, a4
        addi       a5, a5, -1

        vsetvli    t2, zero, e64, m8, ta, ma
        vmv.v.x    v16, zero
2:
        vsetvli    t1, t0, e16, m2, ta, ma
        vle16.v    v0, (a0)
        vle16.v    v2, (a2)

        slli       t2, t1, 1
        sub        t0, t0, t1

        vmaxu.vv   v4, v0, v2
        vminu.vv   v6, v0, v2
        add        a2, a2, t2
        vwsubu.vv  v8, v4, v6
        add        a0, a0, t2

        vsetvli    zero, t1, e32, m4, tu, ma
        vwaddu.wv  v16, v16, v8

        bnez       t0, 2b

        add        a0, a0, a1
        add        a2, a2, a3

        vsetvli    t0, zero, e64, m8, ta, ma
        vadd.vv    v24, v24, v16

        bnez       a5, 1b

        vsetvli    t0, zero, e64, m8, ta, ma
        vmv.s.x    v0, zero
        vredsum.vs v0, v24, v0
        vmv.x.s    a7, v0

        sd         a7, 0(a6)
        ret
endfunc
#endif
