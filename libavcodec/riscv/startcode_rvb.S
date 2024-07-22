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

        .macro  lx rd, addr
#if (__riscv_xlen == 32)
        lw      \rd, \addr
#elif (__riscv_xlen == 64)
        ld      \rd, \addr
#else
        lq      \rd, \addr
#endif
        .endm

func ff_startcode_find_candidate_rvb, zbb
        lpad    0
        add     a1, a0, a1

        // Potentially unaligned head
        andi    t0, a0, -(__riscv_xlen / 8)
        beq     a0, a1, 2f

        andi    t1, a0, (__riscv_xlen / 8) - 1
        lx      t2, (t0)
        li      t3, __riscv_xlen
        orc.b   t2, t2
        slli    t1, t1, 3
        not     t2, t2
        sub     t3, t3, t1
        srl     t2, t2, t1
        addi    t0, t0, __riscv_xlen / 8
        sll     t2, t2, t1
        bnez    t2, 4f

        // Main loop (including potentially short tail)
        bge     t0, a1, 2f
        li      t3, -1
1:
        lx      t2, (t0)
        addi    t0, t0, __riscv_xlen / 8
        orc.b   t2, t2
        bne     t2, t3, 3f // t2 != -1 iff (at least one) zero byte
        blt     t0, a1, 1b

2:      // No zero byte found
        sub     a0, a1, a0
        ret

3:      // Zero byte found in main loop
        not     t2, t2
4:      // Zero byte found in head
        ctz     t2, t2
        addi    t0, t0, -(__riscv_xlen / 8) // back-track
        srl     t2, t2, 3
        add     t0, t0, t2
        // Uncomment the following line for exact POSIX C strnlen() semantics.
        //minu    t0, t0, a1 // ignore zero byte in tail
        sub     a0, t0, a0
        ret
endfunc
