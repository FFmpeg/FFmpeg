/*
 * Bluetooth low-complexity, subband codec (SBC)
 *
 * Copyright (C) 2017  Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2008-2010  Nokia Corporation
 * Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
 * Copyright (C) 2004-2005  Henryk Ploetz <henryk@ploetzli.ch>
 * Copyright (C) 2005-2006  Brad Midgley <bmidgley@xmission.com>
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

/**
 * @file
 * SBC ARMv6 optimizations. The instructions are scheduled for ARM11 pipeline.
 */

#include "libavutil/arm/asm.S"

function ff_sbc_analyze_4_armv6, export=1
        @ r0 = in, r1 = out, r2 = consts
        push            {r1, r3-r7, lr}
        push            {r8-r12, r14}
        ldrd            r4,  r5,  [r0, #0]
        ldrd            r6,  r7,  [r2, #0]
        ldrd            r8,  r9,  [r0, #16]
        ldrd            r10, r11, [r2, #16]
        mov             r14, #0x8000
        smlad           r3,  r4,  r6,  r14
        smlad           r12, r5,  r7,  r14
        ldrd            r4,  r5,  [r0, #32]
        ldrd            r6,  r7,  [r2, #32]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #48]
        ldrd            r10, r11, [r2, #48]
        smlad           r3,  r4,  r6,  r3
        smlad           r12, r5,  r7,  r12
        ldrd            r4,  r5,  [r0, #64]
        ldrd            r6,  r7,  [r2, #64]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #8]
        ldrd            r10, r11, [r2, #8]
        smlad           r3,  r4,  r6,  r3        @ t1[0] is done
        smlad           r12, r5,  r7,  r12       @ t1[1] is done
        ldrd            r4,  r5,  [r0, #24]
        ldrd            r6,  r7,  [r2, #24]
        pkhtb           r3,  r12, r3, asr #16    @ combine t1[0] and t1[1]
        smlad           r12, r8,  r10, r14
        smlad           r14, r9,  r11, r14
        ldrd            r8,  r9,  [r0, #40]
        ldrd            r10, r11, [r2, #40]
        smlad           r12, r4,  r6,  r12
        smlad           r14, r5,  r7,  r14
        ldrd            r4,  r5,  [r0, #56]
        ldrd            r6,  r7,  [r2, #56]
        smlad           r12, r8,  r10, r12
        smlad           r14, r9,  r11, r14
        ldrd            r8,  r9,  [r0, #72]
        ldrd            r10, r11, [r2, #72]
        smlad           r12, r4,  r6,  r12
        smlad           r14, r5,  r7,  r14
        ldrd            r4,  r5,  [r2, #80]      @ start loading cos table
        smlad           r12, r8,  r10, r12       @ t1[2] is done
        smlad           r14, r9,  r11, r14       @ t1[3] is done
        ldrd            r6,  r7,  [r2, #88]
        ldrd            r8,  r9,  [r2, #96]
        ldrd            r10, r11, [r2, #104]     @ cos table fully loaded
        pkhtb           r12, r14, r12, asr #16   @ combine t1[2] and t1[3]
        smuad           r4,  r3,  r4
        smuad           r5,  r3,  r5
        smlad           r4,  r12, r8,  r4
        smlad           r5,  r12, r9,  r5
        smuad           r6,  r3,  r6
        smuad           r7,  r3,  r7
        smlad           r6,  r12, r10, r6
        smlad           r7,  r12, r11, r7
        pop             {r8-r12, r14}
        stmia           r1, {r4, r5, r6, r7}
        pop             {r1, r3-r7, pc}
endfunc

function ff_sbc_analyze_8_armv6, export=1
        @ r0 = in, r1 = out, r2 = consts
        push            {r1, r3-r7, lr}
        push            {r8-r12, r14}
        ldrd            r4,  r5,  [r0, #24]
        ldrd            r6,  r7,  [r2, #24]
        ldrd            r8,  r9,  [r0, #56]
        ldrd            r10, r11, [r2, #56]
        mov             r14, #0x8000
        smlad           r3,  r4,  r6,  r14
        smlad           r12, r5,  r7,  r14
        ldrd            r4,  r5,  [r0, #88]
        ldrd            r6,  r7,  [r2, #88]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #120]
        ldrd            r10, r11, [r2, #120]
        smlad           r3,  r4,  r6,  r3
        smlad           r12, r5,  r7,  r12
        ldrd            r4,  r5,  [r0, #152]
        ldrd            r6,  r7,  [r2, #152]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #16]
        ldrd            r10, r11, [r2, #16]
        smlad           r3,  r4,  r6,  r3        @ t1[6] is done
        smlad           r12, r5,  r7,  r12       @ t1[7] is done
        ldrd            r4,  r5,  [r0, #48]
        ldrd            r6,  r7,  [r2, #48]
        pkhtb           r3,  r12, r3, asr #16    @ combine t1[6] and t1[7]
        str             r3,  [sp, #-4]!          @ save to stack
        smlad           r3,  r8,  r10, r14
        smlad           r12, r9,  r11, r14
        ldrd            r8,  r9,  [r0, #80]
        ldrd            r10, r11, [r2, #80]
        smlad           r3,  r4,  r6,  r3
        smlad           r12, r5,  r7,  r12
        ldrd            r4,  r5,  [r0, #112]
        ldrd            r6,  r7,  [r2, #112]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #144]
        ldrd            r10, r11, [r2, #144]
        smlad           r3,  r4,  r6,  r3
        smlad           r12, r5,  r7,  r12
        ldrd            r4,  r5,  [r0, #0]
        ldrd            r6,  r7,  [r2, #0]
        smlad           r3,  r8,  r10, r3        @ t1[4] is done
        smlad           r12, r9,  r11, r12       @ t1[5] is done
        ldrd            r8,  r9,  [r0, #32]
        ldrd            r10, r11, [r2, #32]
        pkhtb           r3,  r12, r3, asr #16    @ combine t1[4] and t1[5]
        str             r3,  [sp, #-4]!          @ save to stack
        smlad           r3,  r4,  r6,  r14
        smlad           r12, r5,  r7,  r14
        ldrd            r4,  r5,  [r0, #64]
        ldrd            r6,  r7,  [r2, #64]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #96]
        ldrd            r10, r11, [r2, #96]
        smlad           r3,  r4,  r6,  r3
        smlad           r12, r5,  r7,  r12
        ldrd            r4,  r5,  [r0, #128]
        ldrd            r6,  r7,  [r2, #128]
        smlad           r3,  r8,  r10, r3
        smlad           r12, r9,  r11, r12
        ldrd            r8,  r9,  [r0, #8]
        ldrd            r10, r11, [r2, #8]
        smlad           r3,  r4,  r6,  r3        @ t1[0] is done
        smlad           r12, r5,  r7,  r12       @ t1[1] is done
        ldrd            r4,  r5,  [r0, #40]
        ldrd            r6,  r7,  [r2, #40]
        pkhtb           r3,  r12, r3, asr #16    @ combine t1[0] and t1[1]
        smlad           r12, r8,  r10, r14
        smlad           r14, r9,  r11, r14
        ldrd            r8,  r9,  [r0, #72]
        ldrd            r10, r11, [r2, #72]
        smlad           r12, r4,  r6,  r12
        smlad           r14, r5,  r7,  r14
        ldrd            r4,  r5,  [r0, #104]
        ldrd            r6,  r7,  [r2, #104]
        smlad           r12, r8,  r10, r12
        smlad           r14, r9,  r11, r14
        ldrd            r8,  r9,  [r0, #136]
        ldrd            r10, r11, [r2, #136]!
        smlad           r12, r4,  r6,  r12
        smlad           r14, r5,  r7,  r14
        ldrd            r4,  r5,  [r2, #(160 - 136 + 0)]
        smlad           r12, r8,  r10, r12       @ t1[2] is done
        smlad           r14, r9,  r11, r14       @ t1[3] is done
        ldrd            r6,  r7,  [r2, #(160 - 136 + 8)]
        smuad           r4,  r3,  r4
        smuad           r5,  r3,  r5
        pkhtb           r12, r14, r12, asr #16   @ combine t1[2] and t1[3]
                                                 @ r3  = t2[0:1]
                                                 @ r12 = t2[2:3]
        pop             {r0, r14}                @ t2[4:5], t2[6:7]
        ldrd            r8,  r9,  [r2, #(160 - 136 + 32)]
        smuad           r6,  r3,  r6
        smuad           r7,  r3,  r7
        ldrd            r10, r11, [r2, #(160 - 136 + 40)]
        smlad           r4,  r12, r8,  r4
        smlad           r5,  r12, r9,  r5
        ldrd            r8,  r9,  [r2, #(160 - 136 + 64)]
        smlad           r6,  r12, r10, r6
        smlad           r7,  r12, r11, r7
        ldrd            r10, r11, [r2, #(160 - 136 + 72)]
        smlad           r4,  r0,  r8,  r4
        smlad           r5,  r0,  r9,  r5
        ldrd            r8,  r9,  [r2, #(160 - 136 + 96)]
        smlad           r6,  r0,  r10, r6
        smlad           r7,  r0,  r11, r7
        ldrd            r10, r11, [r2, #(160 - 136 + 104)]
        smlad           r4,  r14, r8,  r4
        smlad           r5,  r14, r9,  r5
        ldrd            r8,  r9,  [r2, #(160 - 136 + 16 + 0)]
        smlad           r6,  r14, r10, r6
        smlad           r7,  r14, r11, r7
        ldrd            r10, r11, [r2, #(160 - 136 + 16 + 8)]
        stmia           r1!, {r4, r5}
        smuad           r4,  r3,  r8
        smuad           r5,  r3,  r9
        ldrd            r8,  r9,  [r2, #(160 - 136 + 16 + 32)]
        stmia           r1!, {r6, r7}
        smuad           r6,  r3,  r10
        smuad           r7,  r3,  r11
        ldrd            r10, r11, [r2, #(160 - 136 + 16 + 40)]
        smlad           r4,  r12, r8,  r4
        smlad           r5,  r12, r9,  r5
        ldrd            r8,  r9,  [r2, #(160 - 136 + 16 + 64)]
        smlad           r6,  r12, r10, r6
        smlad           r7,  r12, r11, r7
        ldrd            r10, r11, [r2, #(160 - 136 + 16 + 72)]
        smlad           r4,  r0,  r8,  r4
        smlad           r5,  r0,  r9,  r5
        ldrd            r8,  r9,  [r2, #(160 - 136 + 16 + 96)]
        smlad           r6,  r0,  r10, r6
        smlad           r7,  r0,  r11, r7
        ldrd            r10, r11, [r2, #(160 - 136 + 16 + 104)]
        smlad           r4,  r14, r8,  r4
        smlad           r5,  r14, r9,  r5
        smlad           r6,  r14, r10, r6
        smlad           r7,  r14, r11, r7
        pop             {r8-r12, r14}
        stmia           r1!, {r4, r5, r6, r7}
        pop             {r1, r3-r7, pc}
endfunc
