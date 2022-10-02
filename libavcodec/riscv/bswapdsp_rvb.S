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

#include "config.h"
#include "libavutil/riscv/asm.S"

#if (__riscv_xlen >= 64)
func ff_bswap32_buf_rvb, zbb
        andi    t0, a1, 4
        beqz    t0, 1f
        /* Align a1 (input) to 64-bit */
        lwu     t0, (a1)
        addi    a0, a0, 4
        rev8    t0, t0
        addi    a2, a2, -1
        srli    t0, t0, __riscv_xlen - 32
        addi    a1, a1, 4
        sw      t0, -4(a0)
1:
        andi    a3, a2, -2
        sh2add  a2, a2, a0
        beqz    a3, 3f
        sh2add  a3, a3, a0
2:      /* 2 elements (64 bits) at a time on a 64-bit boundary */
        ld      t0,  (a1)
        addi    a0, a0, 8
        rev8    t0, t0
#if (__riscv_xlen == 64)
        srli    t2, t0, 32
        sw      t0, -4(a0)
#else
        srli    t1, t0, __riscv_xlen - 64
        srli    t2, t0, __riscv_xlen - 32
        sw      t1, -4(a0)
#endif
        addi    a1, a1, 8
        sw      t2, -8(a0)
        bne     a0, a3, 2b
3:
        beq     a0, a2, 5f
4:      /* Process last element */
        lwu     t0, (a1)
        addi    a0, a0, 4
        rev8    t0, t0
        addi    a1, a1, 4
        srli    t0, t0, __riscv_xlen - 32
        sw      t0, -4(a0)
5:
        ret
endfunc
#endif
