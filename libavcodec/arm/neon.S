/*
 * Copyright (c) 2008 Mans Rullgard <mans@mansr.com>
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

.macro  transpose_8x8   r0, r1, r2, r3, r4, r5, r6, r7
        vtrn.32         \r0, \r4
        vtrn.32         \r1, \r5
        vtrn.32         \r2, \r6
        vtrn.32         \r3, \r7
        vtrn.16         \r0, \r2
        vtrn.16         \r1, \r3
        vtrn.16         \r4, \r6
        vtrn.16         \r5, \r7
        vtrn.8          \r0, \r1
        vtrn.8          \r2, \r3
        vtrn.8          \r4, \r5
        vtrn.8          \r6, \r7
.endm

.macro  transpose_4x4   r0, r1, r2, r3
        vtrn.16         \r0, \r2
        vtrn.16         \r1, \r3
        vtrn.8          \r0, \r1
        vtrn.8          \r2, \r3
.endm

.macro  swap4           r0, r1, r2, r3, r4, r5, r6, r7
        vswp            \r0, \r4
        vswp            \r1, \r5
        vswp            \r2, \r6
        vswp            \r3, \r7
.endm

.macro  transpose16_4x4 r0, r1, r2, r3, r4, r5, r6, r7
        vtrn.32         \r0, \r2
        vtrn.32         \r1, \r3
        vtrn.32         \r4, \r6
        vtrn.32         \r5, \r7
        vtrn.16         \r0, \r1
        vtrn.16         \r2, \r3
        vtrn.16         \r4, \r5
        vtrn.16         \r6, \r7
.endm
