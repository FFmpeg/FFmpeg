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

#if (__riscv_xlen >= 64)
        .macro  bswap32_rvb out, in, count
        andi    t0, \count, 1
        beqz    t0, 1f
        /* Align input to 64-bit */
        lwu     t0, (\in)
        addi    \out, \out, 4
        rev8    t0, t0
        addi    \count, \count, -4
        srli    t0, t0, __riscv_xlen - 32
        addi    \in, \in, 4
        sw      t0, -4(\out)
1:
        andi    t3, \count, -8
        add     \count, \count, \out
        beqz    t3, 3f
        add     t3, t3, \out
2:      /* 2 elements (64 bits) at a time on a 64-bit boundary */
        ld      t0,  (\in)
        addi    \out, \out, 8
        rev8    t0, t0
#if (__riscv_xlen == 64)
        srli    t2, t0, 32
        sw      t0, -4(\out)
#else
        srli    t1, t0, __riscv_xlen - 64
        srli    t2, t0, __riscv_xlen - 32
        sw      t1, -4(\out)
#endif
        addi    \in, \in, 8
        sw      t2, -8(\out)
        bne     \out, t3, 2b
3:
        beq     \out, \count, 5f
4:      /* Process last element */
        lwu     t0, (\in)
        addi    \out, \out, 4
        rev8    t0, t0
        addi    \in, \in, 4
        srli    t0, t0, __riscv_xlen - 32
        sw      t0, -4(\out)
5:
        ret
        .endm
#endif
