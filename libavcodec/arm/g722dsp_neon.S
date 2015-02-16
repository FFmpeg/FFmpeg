/*
 * ARM NEON optimised DSP functions for G722 coding
 * Copyright (c) 2015 Peter Meerwald <pmeerw@pmeerw.net>
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

function ff_g722_apply_qmf_neon, export=1, align=4
        movrel          r3, qmf_coeffs
        vld1.s16        {d2,d3,d4}, [r0]! /* load prev_samples */
        vld1.s16        {d16,d17,d18}, [r3,:64]! /* load qmf_coeffs */
        vmull.s16       q0, d2, d16
        vmlal.s16       q0, d3, d17
        vmlal.s16       q0, d4, d18

        vld1.s16        {d5,d6,d7}, [r0]! /* load prev_samples */
        vld1.s16        {d19,d20,d21}, [r3,:64]! /* load qmf_coeffs */
        vmlal.s16       q0, d5, d19
        vmlal.s16       q0, d6, d20
        vmlal.s16       q0, d7, d21

        vadd.s32        d0, d1, d0
        vrev64.32       d0, d0
        vst1.s32        {d0}, [r1]
        bx              lr
endfunc

const qmf_coeffs, align=4
        .hword          3
        .hword          -11
        .hword          -11
        .hword          53
        .hword          12
        .hword          -156
        .hword          32
        .hword          362
        .hword          -210
        .hword          -805
        .hword          951
        .hword          3876
        .hword          3876
        .hword          951
        .hword          -805
        .hword          -210
        .hword          362
        .hword          32
        .hword          -156
        .hword          12
        .hword          53
        .hword          -11
        .hword          -11
        .hword          3
endconst
