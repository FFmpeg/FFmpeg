/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "asm.S"

function ff_ac3_bit_alloc_calc_bap_armv6, export=1
        ldr             r12, [sp]
        cmp             r12, #-960
        beq             4f
        push            {r4-r11,lr}
        add             r5,  sp,  #40
        movrel          r4,  X(ff_ac3_bin_to_band_tab)
        movrel          lr,  X(ff_ac3_band_start_tab)
        ldm             r5,  {r5-r7}
        ldrb            r4,  [r4, r2]
        add             r1,  r1,  r2,  lsl #1           @ psd + start
        add             r0,  r0,  r4,  lsl #1           @ mask + band
        add             r4,  lr,  r4
        add             r7,  r7,  r2                    @ bap + start
1:
        ldrsh           r9,  [r0], #2                   @ mask[band]
        mov             r8,  #0xff0
        sub             r9,  r9,  r12                   @   - snr_offset
        ldrb            r10, [r4, #1]!                  @ band_start_tab[++band]
        subs            r9,  r9,  r5                    @   - floor
        it              lt
        movlt           r9,  #0
        cmp             r10, r3                         @   - end
        and             r9,  r9,  r8, lsl #1            @   & 0x1fe0
        ite             gt
        subgt           r8,  r3,  r2
        suble           r8,  r10, r2
        mov             r2,  r10
        add             r9,  r9,  r5                    @   + floor => m
        tst             r8,  #1
        add             r11, r7,  r8
        bne             3f
        b               5f
2:
        ldrsh           r8,  [r1], #2
        ldrsh           lr,  [r1], #2
        sub             r8,  r8,  r9
        sub             lr,  lr,  r9
        usat            r8,  #6,  r8,  asr #5           @ address
        usat            lr,  #6,  lr,  asr #5
        ldrb            r8,  [r6, r8]                   @ bap_tab[address]
        ldrb            lr,  [r6, lr]
        strb            r8,  [r7], #1                   @ bap[bin]
        strb            lr,  [r7], #1
5:      cmp             r7,  r11
        blo             2b
        cmp             r3,  r10
        bgt             1b
        pop             {r4-r11,pc}
3:
        ldrsh           r8,  [r1], #2                   @ psd[bin]
        sub             r8,  r8,  r9                    @   - m
        usat            r8,  #6,  r8,  asr #5           @ address
        ldrb            r8,  [r6, r8]                   @ bap_tab[address]
        strb            r8,  [r7], #1                   @ bap[bin]
        b               5b
4:
        ldr             r0,  [sp, #12]
        mov             r1,  #0
        mov             r2,  #256
        b               X(memset)
endfunc
