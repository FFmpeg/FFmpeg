/*
 * Copyright (c) 2011 Mans Rullgard <mans@mansr.com>
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

#include "libavutil/arm/asm.S"

function ff_ac3_update_bap_counts_arm, export=1
        push            {lr}
        ldrb            lr,  [r1], #1
1:
        lsl             r3,  lr,  #1
        ldrh            r12, [r0, r3]
        subs            r2,  r2,  #1
        it              gt
        ldrbgt          lr,  [r1], #1
        add             r12, r12, #1
        strh            r12, [r0, r3]
        bgt             1b
        pop             {pc}
endfunc
