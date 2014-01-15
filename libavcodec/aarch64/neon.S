/*
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

.macro  transpose_4x4H  r0, r1, r2, r3, r4, r5, r6, r7
        trn1            \r4\().4H,  \r0\().4H,  \r1\().4H
        trn2            \r5\().4H,  \r0\().4H,  \r1\().4H
        trn1            \r7\().4H,  \r3\().4H,  \r2\().4H
        trn2            \r6\().4H,  \r3\().4H,  \r2\().4H
        trn1            \r0\().2S,  \r4\().2S,  \r7\().2S
        trn2            \r3\().2S,  \r4\().2S,  \r7\().2S
        trn1            \r1\().2S,  \r5\().2S,  \r6\().2S
        trn2            \r2\().2S,  \r5\().2S,  \r6\().2S
.endm

.macro  transpose_8x8H  r0, r1, r2, r3, r4, r5, r6, r7, r8, r9
        trn1            \r8\().8H,  \r0\().8H,  \r1\().8H
        trn2            \r9\().8H,  \r0\().8H,  \r1\().8H
        trn1            \r1\().8H,  \r2\().8H,  \r3\().8H
        trn2            \r3\().8H,  \r2\().8H,  \r3\().8H
        trn1            \r0\().8H,  \r4\().8H,  \r5\().8H
        trn2            \r5\().8H,  \r4\().8H,  \r5\().8H
        trn1            \r2\().8H,  \r6\().8H,  \r7\().8H
        trn2            \r7\().8H,  \r6\().8H,  \r7\().8H

        trn1            \r4\().4S,  \r0\().4S,  \r2\().4S
        trn2            \r2\().4S,  \r0\().4S,  \r2\().4S
        trn1            \r6\().4S,  \r5\().4S,  \r7\().4S
        trn2            \r7\().4S,  \r5\().4S,  \r7\().4S
        trn1            \r5\().4S,  \r9\().4S,  \r3\().4S
        trn2            \r9\().4S,  \r9\().4S,  \r3\().4S
        trn1            \r3\().4S,  \r8\().4S,  \r1\().4S
        trn2            \r8\().4S,  \r8\().4S,  \r1\().4S

        trn1            \r0\().2D,  \r3\().2D,  \r4\().2D
        trn2            \r4\().2D,  \r3\().2D,  \r4\().2D

        trn1            \r1\().2D,  \r5\().2D,  \r6\().2D
        trn2            \r5\().2D,  \r5\().2D,  \r6\().2D

        trn2            \r6\().2D,  \r8\().2D,  \r2\().2D
        trn1            \r2\().2D,  \r8\().2D,  \r2\().2D

        trn1            \r3\().2D,  \r9\().2D,  \r7\().2D
        trn2            \r7\().2D,  \r9\().2D,  \r7\().2D

.endm
