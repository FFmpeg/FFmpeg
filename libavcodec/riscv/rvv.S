/*
 * Copyright (c) 2026 Jia Yuan <yuan.jia@sanechips.com.cn>
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

// v0 must be 0x55 as mask
.macro TRANSPOSE_8x8 es, d0, d1, d2, d3, d4, d5, d6, d7, r0, r1, r2, r3, r4, r5, r6, r7
    vslideup.vi     \d0, \r1, 1
    vslidedown.vi   \d1, \r0, 1
    vslideup.vi     \d2, \r3, 1
    vslidedown.vi   \d3, \r2, 1
    vslideup.vi     \d4, \r5, 1
    vslidedown.vi   \d5, \r4, 1
    vslideup.vi     \d6, \r7, 1
    vslidedown.vi   \d7, \r6, 1
    vmerge.vvm      \d0, \d0, \r0, v0
    vmerge.vvm      \d1, \r1, \d1, v0
    vmerge.vvm      \d2, \d2, \r2, v0
    vmerge.vvm      \d3, \r3, \d3, v0
    vmerge.vvm      \d4, \d4, \r4, v0
    vmerge.vvm      \d5, \r5, \d5, v0
    vmerge.vvm      \d6, \d6, \r6, v0
    vmerge.vvm      \d7, \r7, \d7, v0

.if \es == 16
    vsetivli        zero, 4, e32, m1, ta, ma
.elseif \es == 8
    vsetivli        zero, 4, e16, m1, ta, ma
.endif

    vslideup.vi     \r0, \d2, 1
    vslidedown.vi   \r1, \d0, 1
    vslideup.vi     \r2, \d3, 1
    vslidedown.vi   \r3, \d1, 1
    vslideup.vi     \r4, \d6, 1
    vslidedown.vi   \r5, \d4, 1
    vslideup.vi     \r6, \d7, 1
    vslidedown.vi   \r7, \d5, 1
    vmerge.vvm      \r0, \r0, \d0, v0
    vmerge.vvm      \r1, \d2, \r1, v0
    vmerge.vvm      \r2, \r2, \d1, v0
    vmerge.vvm      \r3, \d3, \r3, v0
    vmerge.vvm      \r4, \r4, \d4, v0
    vmerge.vvm      \r5, \d6, \r5, v0
    vmerge.vvm      \r6, \r6, \d5, v0
    vmerge.vvm      \r7, \d7, \r7, v0

.if \es == 16
    vsetivli        zero, 2, e64, m1, ta, ma
.elseif \es == 8
    vsetivli        zero, 2, e32, m1, ta, ma
.endif

    vslideup.vi     \d0, \r4, 1
    vslidedown.vi   \d4, \r0, 1
    vslideup.vi     \d1, \r6, 1
    vslidedown.vi   \d5, \r2, 1
    vslideup.vi     \d2, \r5, 1
    vslidedown.vi   \d6, \r1, 1
    vslideup.vi     \d3, \r7, 1
    vslidedown.vi   \d7, \r3, 1
    vmerge.vvm      \d0, \d0, \r0, v0
    vmerge.vvm      \d4, \r4, \d4, v0
    vmerge.vvm      \d1, \d1, \r2, v0
    vmerge.vvm      \d5, \r6, \d5, v0
    vmerge.vvm      \d2, \d2, \r1, v0
    vmerge.vvm      \d6, \r5, \d6, v0
    vmerge.vvm      \d3, \d3, \r3, v0
    vmerge.vvm      \d7, \r7, \d7, v0
.endm
